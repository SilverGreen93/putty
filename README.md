# PuTTY Plus

PuTTY Plus is a fork of PuTTY (https://git.tartarus.org/simon/putty.git) which aims to keep up-to-date with the original project, but add small extra quality of life features.

## Latest release

[![GitHub release (latest by date)](https://img.shields.io/github/v/release/SilverGreen93/putty)](https://github.com/SilverGreen93/putty/releases/latest) [![Downloads (Latest Release)](https://img.shields.io/github/downloads/SilverGreen93/putty/latest/total?label=latest%20release%20downloads)](https://github.com/SilverGreen93/putty/releases/latest) ![GitHub all releases](https://img.shields.io/github/downloads/SilverGreen93/putty/total?label=total%20downloads)

All releases are available for download at https://github.com/SilverGreen93/putty/releases

Documentation is available at https://www.chiark.greenend.org.uk/~sgtatham/putty/docs.html

## Features

In addition to all features in PuTTY, the following were added:

- Mouse scroll wheel changes font size when used with Control key. Always active.
- The number of scrolled lines can be customized per mouse scroll wheel tick to offer a more smooth scrolling experience. See Window settings for configuration.
- Connection errors now display in the terminal instead of a separate message box, to prevent user disruption when having multiple terminals opened.

## Colour themes

The PuTTY Plus release comes with 3 colour themes available in a separate [registry file](putty_themes.reg):

- Color server (included also in the Default Settings)
- Green console
- Orange console

When installing the themes, they will overwrite any existing PuTTY themes with the same name.

The recomended font for the terminal is [JetBrains Mono](https://github.com/ryanoasis/nerd-fonts/releases/download/v3.4.0/JetBrainsMono.zip).

## Building and issue tracking

The project was built with `cmake` on Windows using the following tools:
```
-- Building for: Visual Studio 17 2022
-- Selecting Windows SDK version 10.0.26100.0 to target Windows 10.0.26200.
-- The C compiler identification is MSVC 19.44.35223.0
```

The `plus` branch is the main branch of this project. The branch called `main` is an exact copy of Simon's PuTTY repository.

Build instructions:

```
cmake .
cmake --build . --config Debug
```

Clean instructions:

```
cmake --build . --target clean
```

To request new features or report any found issues, use the Issues tab.

## License

MIT

For additional License information please read the LICENSE.txt included with the download
