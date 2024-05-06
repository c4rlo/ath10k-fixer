# ath10k-fixer

## The problem

My Qualcomm Atheros QCA6174 WiFi card has an issue on my Linux machine: frequently, it
will be non-functional after boot.

There are various reports of others experiencing similar issues:
[here](https://bbs.archlinux.org/viewtopic.php?id=266143),
[here](https://bugzilla.redhat.com/show_bug.cgi?id=1846802),
[here](https://askubuntu.com/q/1434288). Sometimes it is suggested to use the most
recent firmware files from https://github.com/kvalo/ath10k-firmware, but I have found
this to not solve the issue in my case.

## The solution

What I did find to work (as mentioned for example
[here](https://old.reddit.com/r/archlinux/comments/sav8f5/my_network_adapter_fails_to_start/hu2tnsj/))
is to reload the `ath10k_pci` kernel module, whenever the system log contains an
indication of the problem; typically, this is a message like "`ath10k_pci […]: could not
init core`").

This repository contains a daemon that automates this, by watching the system log for
relevant error messages, and reloading the kernel module in response. This has been a
satisfactory solution for me.

## Installation

- If using Arch Linux, install [ath10k-fixer from the AUR](https://aur.archlinux.org/packages/ath10k-fixer)
  ([what's the AUR?](https://wiki.archlinux.org/index.php/Arch_User_Repository)).
- Otherwise, run `make install`. This requires that you have a recent GCC with C++
  compiler and GNU Make. You may be prompted for your `sudo` password.
- Finally, run `sudo systemctl enable --now ath10k-fixer`. This starts the daemon and
  ensures it gets started at every boot.

## Uninstallation

- Run `sudo systemctl disable --now ath10k-fixer` to stop the daemon and disable it such
  that it no longer gets started at every boot.
- If you installed ath10k-fixer from the AUR, uninstall it via
  `sudo pacman -Rs ath10k-fixer`.
- If instead you installed it via `make install`, run `make uninstall`. You may be
  prompted for your `sudo` password.

## Development

Prerequisites: Recent GCC with C++ compiler and GNU Make.

- `make debug`: create debug build.
- `make compile_commands.json`: generate [`compile_commands.json`](https://clang.llvm.org/docs/JSONCompilationDatabase.html) file, useful for language servers like [`clangd`](https://clangd.llvm.org/); requires [`bear`](https://github.com/rizsotto/Bear).
- `make release`: create release build.
- `make clean`: delete all build artefacts.
