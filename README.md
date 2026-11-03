# Dodo Store

A DTK-based Flatpak Store Frontend for Deepin Desktop Environment (DDE).

## Why

The distro *deepin* does come with a store application, which support its own application repository and Linyaps applications. But sometimes deepin users might still want to use applications from Flathub or other flatpak remotes. Additionally, non-deepin distros (e.g. Arch Linux, UbuntuDDE, Debian) if they want to port DDE, they don't really have a flatpak store frontend that integrated with the desktop environment itself.

## Building

### Dependencies

Before building, you'll need to get all the dependencies installed, which includes:

- Qt 6
- DTK 6
- AppStreamQt for Qt 6
- libflatpak

You can take `.github/workflows/deepin.yml`'s "Get build deps" CI step as a reference.

### Building

Generic CMake method applies:

```shell
$ cmake -Bbuild .     # Configuration. It's suggested to add -DCMAKE_EXPORT_COMPILE_COMMANDS=ON as well
$ cmake --build build # Build
$ ./build/dodo-store  # Launch built binary
```

### Packaging via CPack

Once built, you can use `cpack` to create a `deb` package if preferred:

```shell
$ cd build
$ cpack -G DEB
```

## Contributing

Dodo Store is a community-driven project that welcomes all users to contribute. All kinds of contribution are welcome, which include translation, code changes, bug reports and feature discussion. Feel free to directly create a Pull Request to contribute.

## License

LGPL-3.0-or-later
