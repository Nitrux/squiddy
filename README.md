# Squiddy | [![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)

A straightforward installer for **[Nitrux](https://nxos.org/)**.

# Introduction

Squiddy is the graphical installer for Nitrux. It guides users through system setup and installation.

Squiddy is built with **[MauiKit](https://mauikit.org/)**.

## Features

- Guided setup:
  - Select language, locale, and keyboard settings
  - Create a user account and configure login options
  - Select an installation disk and review the proposed storage layout

- Installation checks:
  - Check GPT disk suitability and installation requirements before writing
  - Support Nitrux images using DwarFS or SquashFS

- Secure authorization:
  - Run the graphical installer as the logged-in user
  - Use Polkit to authorize installation

- Nitrux systems:
  - Target UEFI systems with GPT partitioning
  - Use XFS for `/` and F2FS for `/home` and `/var/lib`
  - Support Nitrux live images using OpenRC

## Requirements

- Nitrux 7.1.0 and newer.

### Runtime Requirements

```
dbus
dosfstools
dwarfs-tools
efibootmgr
f2fs-tools
libpwquality1
locales
mauikit (>= 4.0.0)
polkitd
qml6-module-qtquick
qml6-module-qtquick-controls
qml6-module-qtquick-layouts
qml6-module-qtquick-window
squashfs-tools
tzdata
udev
util-linux
xfsprogs
xkb-data
```

# Usage

Launch Squiddy from the applications menu.

# Licensing

The license for this repository and its contents is **BSD-3-Clause**.

# Issues

If you find a problem with Squiddy, please create an issue using the **🐞 Bug report** template.

## Submitting a bug report

Before submitting a bug, check the [existing bug reports](https://github.com/Nitrux/squiddy/issues) to see whether it has already been reported.

©2026 Nitrux Latinoamericana S.C.
