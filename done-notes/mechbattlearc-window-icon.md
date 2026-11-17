# Request from MechBattleArc (Molten Breach): window and taskbar icon from the exe

**Now.** In `plat_win32.c`, the window class uses `wc.hIcon = LoadIcon(NULL, IDI_APPLICATION)`. A game that links an
icon resource into its exe gets that icon in Explorer, but the running window's title bar, the taskbar and Alt-Tab
still show the generic application icon.

**Ask.** Use the exe's own icon when it has one, and fall back to the generic icon:

    HICON ic = LoadIcon(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    wc.hIcon = ic ? ic : LoadIcon(NULL, IDI_APPLICATION);
    /* and wc.hIconSm through LoadImageW(..., IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), ...) for a crisp 16 px */

Resource ID 1 is the usual convention. Molten Breach's `res\moltenbreach.rc` already uses `1 ICON "moltenbreach.ico"`,
linked by build.bat. This is a one-line change and does nothing to games without an icon resource.

**Optional.** `build.bat` in ARC's templates could link `res\*.rc` when present, so every game gets the same step.
