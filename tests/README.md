# Tests

Keep shared tests and build settings in this directory, reusable SDK stubs in
`stubs/`, and cartridge-specific sources and build rules in a cartridge
subdirectory such as `ed64_pro/`.

Run host tests from the repository root:

```sh
make -f tests/Makefile test-rom-info test-rom-info-sanitize
make -f tests/Makefile test-rtc-view test-rtc-view-sanitize
make -f tests/Makefile test-ed64-pro test-ed64-pro-sanitize
```

The shared Makefile includes `ed64_pro/host.mk`. The Pro hardware ROM and menu
test builds use `ed64_pro/Makefile.n64`, `ed64_pro/Makefile.region`,
`ed64_pro/Makefile.region-probe`, and `ed64_pro/Makefile.startup`.
