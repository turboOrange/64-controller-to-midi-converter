{
  description = "64-Controller-to-MIDI Converter — RP2040 firmware + KiCad hardware dev shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          name = "64-controller-to-midi";

          packages = with pkgs; [
            # ── ARM bare-metal toolchain (arm-none-eabi-gcc/g++/objcopy/…) ──
            gcc-arm-embedded

            # ── Build system ─────────────────────────────────────────────────
            cmake    # 3.21+ required by firmware/CMakeLists.txt
            ninja    # fast parallel builds

            # ── Raspberry Pi Pico SDK ─────────────────────────────────────────
            # The nixpkgs package ships a setup hook that automatically exports
            # PICO_SDK_PATH, satisfying the include() in firmware/CMakeLists.txt.
            # pioasm (PIO assembler) is built and available on PATH from this
            # package as well.
            pico-sdk

            # ── Python 3 ─────────────────────────────────────────────────────
            # Required by Pico SDK CMake helper scripts (e.g. GeneratePioHeader)
            # and by pioasm code-generation paths.
            python3

            # ── Flashing & on-chip debugging ──────────────────────────────────
            picotool    # UF2 / flash / info tool for RP2040
            openocd     # SWD/JTAG debug bridge (Raspberry Pi probe, J-Link, …)
            # arm-none-eabi-gdb is bundled inside gcc-arm-embedded above

            # ── Hardware design (KiCad EDA) ───────────────────────────────────
            kicad       # schematic capture + PCB layout for /hardware/

            # ── Documentation ─────────────────────────────────────────────────
            doxygen     # generate API docs from Doxygen-style comments

            # ── Version control & utilities ───────────────────────────────────
            git         # manage submodules under firmware/lib/
            minicom     # serial monitor — useful for UART debug output
          ];

          shellHook = ''
            # The nixpkgs pico-sdk package setup hook only runs inside derivation
            # builds.  Export PICO_SDK_PATH explicitly here so cmake can find the
            # SDK in interactive shells AND in `nix develop --command` invocations.
            export PICO_SDK_PATH="${pkgs.pico-sdk}/lib/pico-sdk"

            echo ""
            echo "🎵  64-Controller-to-MIDI — dev shell ready"
            echo "────────────────────────────────────────────────────────────"
            printf "  Toolchain  : %s\n" "$(arm-none-eabi-gcc --version | head -1)"
            printf "  CMake      : %s\n" "$(cmake --version | head -1)"
            printf "  Pico SDK   : %s\n" "''${PICO_SDK_PATH}"
            echo "────────────────────────────────────────────────────────────"
            echo "  Build       :  cd firmware && cmake -B build -G Ninja && ninja -C build"
            echo "  Flash (UF2) :  picotool load -f firmware/build/n64_midi_converter.uf2"
            echo "────────────────────────────────────────────────────────────"
            echo ""
          '';
        };
      }
    );
}

