/*
  Copyright 2009-2010, Ifcaro, jimmikaelkael & Polo
  Copyright 2006-2008 Polo
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.

  Some parts of the code are taken from HD Project by Polo
*/

#include <iopcontrol.h>

#include "ee_core.h"
#include "iopmgr.h"
#include "modules.h"
#include "modmgr.h"
#include "util.h"
#include "syshook.h"

extern int _iop_reboot_count;

static void ResetIopSpecial(const char *args, unsigned int arglen){
	void *pIOP_buffer, *IOPRP_img, *imgdrv_irx;
	unsigned int length_rounded, CommandLen, size_IOPRP_img, size_imgdrv_irx;
	char command[RESET_ARG_MAX+1];

	if(arglen>0){
		strncpy(command, args, arglen);
		command[arglen] = '\0';	/* In a normal IOP reset process, the IOP reset command line will be NULL-terminated properly somewhere.
						Since we're now taking things into our own hands, NULL terminate it here.
						Some games like SOCOM3 will use a command line that isn't NULL terminated, resulting in things like "cdrom0:\RUN\IRX\DNAS300.IMGG;1" */
		_strcpy(&command[arglen+1], "img0:");
		CommandLen=arglen+6;
	}
	else{
		_strcpy(command, "img0:");
		CommandLen=5;
	}

	GetOPLModInfo(OPL_MODULE_ID_IOPRP, &IOPRP_img, &size_IOPRP_img);
	GetOPLModInfo(OPL_MODULE_ID_IMGDRV, &imgdrv_irx, &size_imgdrv_irx);

	length_rounded=(size_IOPRP_img+0xF)&~0xF;
	pIOP_buffer=SifAllocIopHeap(length_rounded);

	CopyToIop(IOPRP_img, length_rounded, pIOP_buffer);

	*(void**)(UNCACHED_SEG(&((unsigned char*)imgdrv_irx)[0x180])) = pIOP_buffer;
	*(u32*)(UNCACHED_SEG(&((unsigned char*)imgdrv_irx)[0x184])) = size_IOPRP_img;

	LoadMemModule(0, imgdrv_irx, size_imgdrv_irx, 0, NULL);

	DIntr();
	ee_kmode_enter();
	Old_SifSetReg(SIF_REG_SMFLAG, SIF_STAT_BOOTEND);
	ee_kmode_exit();
	EIntr();

	LoadOPLModule(OPL_MODULE_ID_UDNL, SIF_RPC_M_NOWAIT, CommandLen, command);

	DIntr();
	ee_kmode_enter();
	Old_SifSetReg(SIF_REG_SMFLAG, SIF_STAT_SIFINIT|SIF_STAT_CMDINIT);
	Old_SifSetReg(SIF_SYSREG_RPCINIT, 0);
	Old_SifSetReg(SIF_SYSREG_SUBADDR, (int)NULL);
	ee_kmode_exit();
	EIntr();

	LoadFileExit();	//OPL's integrated LOADFILE RPC does not automatically unbind itself after IOP resets.

	_iop_reboot_count++; // increment reboot counter to allow RPC clients to detect unbinding!

	while (!SifIopSync()) {;}

	SifInitRpc(0);
	SifInitIopHeap();
	LoadFileInit();
	sbv_patch_enable_lmb();

	DPRINTF("Loading extra IOP modules...\n");
	if (GameMode == USB_MODE) {
		LoadOPLModule(OPL_MODULE_ID_USBD, 0, 11, "thpri=15,16");
	}
	else if (GameMode == ETH_MODE) {
		LoadOPLModule(OPL_MODULE_ID_SMSTCPIP, 0, 0, NULL);
		LoadOPLModule(OPL_MODULE_ID_SMAP, 0, g_ipconfig_len, g_ipconfig);
		LoadOPLModule(OPL_MODULE_ID_SMBINIT, 0, 0, NULL);
	}

#ifdef __LOAD_DEBUG_MODULES
	if(GameMode != ETH_MODE) {
		LoadOPLModule(OPL_MODULE_ID_SMSTCPIP, 0, 0, NULL);
		LoadOPLModule(OPL_MODULE_ID_SMAP, 0, g_ipconfig_len, g_ipconfig);
	}
#ifdef __DECI2_DEBUG
	LoadOPLModule(OPL_MODULE_ID_DRVTIF, 0, 0, NULL);
	LoadOPLModule(OPL_MODULE_ID_TIFINET, 0, 0, NULL);
#else
	LoadOPLModule(OPL_MODULE_ID_UDPTTY, 0, 0, NULL);
	LoadOPLModule(OPL_MODULE_ID_IOPTRAP, 0, 0, NULL);
#endif
#endif
}

/*----------------------------------------------------------------------------------------*/
/* Reset IOP to include our modules.							  */
/*----------------------------------------------------------------------------------------*/
int New_Reset_Iop(const char *arg, int arglen)
{
	DPRINTF("New_Reset_Iop start!\n");
	if(!DisableDebug)
		GS_BGCOLOUR = 0xFF00FF;	//Purple

	SifInitRpc(0);

	iop_reboot_count++;

	// Reseting IOP.
	while (!Reset_Iop(NULL, 0)) {;}
	while (!SifIopSync()){;}

	SifInitRpc(0);
	SifInitIopHeap();
	LoadFileInit();
	sbv_patch_enable_lmb();

	ResetIopSpecial(NULL, 0);
	if(!DisableDebug)
		GS_BGCOLOUR = 0x00A5FF;	//Orange

	if(arglen>0){
		ResetIopSpecial(&arg[10], arglen-10);
		if(!DisableDebug)
			GS_BGCOLOUR = 0x00FFFF;	//Yellow
	}

#ifdef VMC
	if (iop_reboot_count >= 2) {
		LoadOPLModule(OPL_MODULE_ID_MCEMU, 0, 0, NULL);
	}
#endif

	/*	For HDD mode, CDVDMAN's name is set to DEV9 (so that the game can find DEV9 loaded) in order for network support to still work.
		However, some games will stop working (for one reason or another) if they find DEV9 loaded when it should not...
		SMB mode never has its CDVDMAN name changed to DEV9 because it cannot share the SMAP interface with the game.	*/
	if (g_compat_mask & COMPAT_MODE_8)
		ChangeModuleName("dev9", "cdvdman");

	DPRINTF("Exiting services...\n");
	SifExitIopHeap();
	LoadFileExit();
	SifExitRpc();

	DPRINTF("New_Reset_Iop complete!\n");
	// we have 4 SifSetReg calls to skip in ELF's SifResetIop, not when we use it ourselves
	if (set_reg_disabled)
		set_reg_hook = 4;

	if(!DisableDebug)
		GS_BGCOLOUR = 0x000000;	//Black

	return 1;
}

/*----------------------------------------------------------------------------------------*/
/* Reset IOP. This function replaces SifIopReset from the PS2SDK                          */
/*----------------------------------------------------------------------------------------*/
int Reset_Iop(const char *arg, int mode)
{
	struct _iop_reset_pkt reset_pkt;  /* Implicitly aligned. */
	struct t_SifDmaTransfer dmat;

	_iop_reboot_count++; // increment reboot counter to allow RPC clients to detect unbinding!

/*	SifStopDma();		For the sake of IGR (Which uses this function), don't disable SIF0 (IOP -> EE)
				because some games will be still spamming DMA transfers across SIF0 when IGR is invoked.
				SCE documents that DMA transfers should be stopped before IOP resets, but has neglected
				to explain the effects of not doing so.
				So far, it seems like the SIF (at least SIF0) will stop functioning properly.

				2 commits before this one, OPL appears to have worked around this problem by preventing
				the SIF BOOTEND flag from being set,
				which allowed SifInitCmd() to run ASAP (Even before the IOP finishes rebooting.
				That caused SifSetDChain() to be run ASAP, which re-enables SIF0.
				I don't find that a good workaround because it may result in a timing problem.	*/

	memset(&reset_pkt, 0, sizeof reset_pkt);

	reset_pkt.header.size = sizeof reset_pkt;
	reset_pkt.header.cid  = SIF_CMD_RESET_CMD;

	reset_pkt.mode = mode;
	if (arg != NULL) {
		strncpy(reset_pkt.arg, arg, RESET_ARG_MAX);
		reset_pkt.arg[RESET_ARG_MAX] = '\0';

		reset_pkt.arglen = strlen(reset_pkt.arg) + 1;
	}

	dmat.src  = &reset_pkt;
	dmat.dest = (void *)SifGetReg(SIF_SYSREG_SUBADDR);
	dmat.size = sizeof(reset_pkt);
	dmat.attr = SIF_DMA_ERT | SIF_DMA_INT_O;
	SifWriteBackDCache(&reset_pkt, sizeof(reset_pkt));

	DIntr();
	ee_kmode_enter();
	Old_SifSetReg(SIF_REG_SMFLAG, SIF_STAT_BOOTEND);

	if (!Old_SifSetDma(&dmat, 1)){
		ee_kmode_exit();
		EIntr();
		return 0;
	}

	Old_SifSetReg(SIF_REG_SMFLAG, SIF_STAT_SIFINIT|SIF_STAT_CMDINIT);
	Old_SifSetReg(SIF_SYSREG_RPCINIT, 0);
	Old_SifSetReg(SIF_SYSREG_SUBADDR, (int)NULL);
	ee_kmode_exit();
	EIntr();

	return 1;
}