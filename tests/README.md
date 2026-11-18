# Tests

Run these commands from the repository root, using the project's normal
development environment with its submodules initialized:

```sh
make -f tests/Makefile test
make -f tests/Makefile test-sanitize
```

The host tests use a C compiler and Make. The sanitizer target also enables
AddressSanitizer and UndefinedBehaviorSanitizer. These are available in the
project's build container; no console, USB utilities, or additional packages
are needed. Test binaries go in `build/tests/`.

The suites cover ROM information, flashcart diagnostics, ED64 Pro transport,
startup and shutdown, storage and loading, RTC, and 64DD loading. Individual
targets are available for `test-rom-info`, `test-flashcart-diagnostics`,
`test-rtc`, and `test-cart-load-dd`; append `-sanitize` to run with sanitizers.
`test-ed64-pro` runs all current suites, including debug and release startup
tests and an injected write failure.

[ED64 Pro hardware tests](ed64_pro/README.md) use the normal libdragon toolchain,
an SD card, and the console. Their results appear on screen.

Keep shared tests and build settings here, reusable SDK stubs in `stubs/`, and
cartridge-specific sources and build rules in a cartridge subdirectory such
as `ed64_pro/`.
