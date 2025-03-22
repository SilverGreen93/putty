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

## Building and issue tracking

The project was built with `cmake` under MSYS2 MINGW64 on Windows.

The `plus` branch is the main branch of this project. The branch called `main` is an exact copy of Simon's PuTTY repository.

Build instructions:

```
cmake .
cmake --build .
```

To request new features or report any found issues, use the Issues tab.

## License

MIT

For additional License information please read the LICENSE.txt included with the download
