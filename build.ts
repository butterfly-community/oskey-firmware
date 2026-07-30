#!/usr/bin/env -S deno run --allow-read --allow-write --allow-run=west

interface Board {
  name: string;
  target: string;
  features: string[];
  overlays?: string[];
  westArgs?: string[];
  cmakeArgs?: string[];
}

const buildDir = "temp";
const outputDir = "boards/build";
const artifacts = ["bin", "elf", "uf2"] as const;

const boards: Board[] = [
  {
    name: "stm32_nucleo_f401re",
    target: "nucleo_f401re",
    features: ["TEST_RNG"],
    cmakeArgs: ["-DCONFIG_HEAP_MEM_POOL_SIZE=40960"],
  },
  {
    name: "esp32s3_devkitc",
    target: "esp32s3_devkitc/esp32s3/procpu",
    features: ["STORAGE"],
  },
  {
    name: "esp32s3_devkitc_fido2",
    target: "esp32s3_devkitc/esp32s3/procpu",
    features: ["FIDO2"],
    overlays: [
      "boards/overlay/cdc_acm.overlay",
      "boards/overlay/fido2.overlay",
    ],
  },
  {
    name: "esp32s3_core",
    target: "esp32s3_devkitc/esp32s3/procpu",
    features: ["STORAGE"],
    overlays: ["boards/overlay/esp32_usb_jtag_serial.overlay"],
  },
  {
    name: "lichuang_szpi_s3",
    target: "esp32s3_devkitc/esp32s3/procpu",
    features: ["STORAGE", "DISPLAY"],
    overlays: ["boards/esp32s3_lichuang.overlay"],
    cmakeArgs: ["-DCONFIG_SPI_INIT_PRIORITY=80"],
  },
  {
    name: "lichuang_szpi_s3_usb_jtag_serial",
    target: "esp32s3_devkitc/esp32s3/procpu",
    features: ["STORAGE", "DISPLAY"],
    overlays: [
      "boards/esp32s3_lichuang.overlay",
      "boards/overlay/esp32_usb_jtag_serial.overlay",
    ],
    cmakeArgs: ["-DCONFIG_SPI_INIT_PRIORITY=80"],
  },
  {
    name: "lichuang_szpi_s3_webusb",
    target: "esp32s3_devkitc/esp32s3/procpu",
    features: ["STORAGE", "DISPLAY", "USB"],
    overlays: [
      "boards/esp32s3_lichuang.overlay",
      "boards/overlay/cdc_acm.overlay",
    ],
    cmakeArgs: ["-DCONFIG_SPI_INIT_PRIORITY=80"],
  },
  {
    name: "generic_esp32e_2.8_ili9341",
    target: "esp32_devkitc/esp32/procpu",
    features: ["STORAGE", "DISPLAY"],
    overlays: ["boards/esp32_32e_2.8_led_display_ili9341.overlay"],
  },
  {
    name: "generic_esp32e_2.8_st7789",
    target: "esp32_devkitc/esp32/procpu",
    features: ["STORAGE", "DISPLAY"],
    overlays: ["boards/esp32_32e_2.8_led_display_st7789.overlay"],
  },
  {
    name: "waveshare_s3_touch_lcd_3.5",
    target: "esp32s3_devkitc/esp32s3/procpu",
    features: ["STORAGE", "DISPLAY"],
    overlays: [
      "boards/esp32s3_waveshare_3.5.overlay",
      "boards/overlay/esp32_usb_jtag_serial.overlay",
    ],
  },
  {
    name: "stm32h747i_disco",
    target: "stm32h747i_disco/stm32h747xx/m7",
    features: ["STORAGE", "DISPLAY"],
    westArgs: ["--shield", "st_b_lcd40_dsi1_mb1166"],
  },
  {
    name: "stm32f769i_disco",
    target: "stm32f769i_disco",
    features: ["STORAGE"],
    overlays: ["boards/overlay/stm32_rng.overlay"],
  },
  {
    name: "nrf52840_mdk",
    target: "nrf52840_mdk",
    features: ["STORAGE"],
  },
];

async function removeDirectory(path: string) {
  try {
    await Deno.remove(path, { recursive: true });
  } catch (error) {
    if (!(error instanceof Deno.errors.NotFound)) {
      throw error;
    }
  }
}

async function copyBuildFiles(boardName: string) {
  for (const fileExt of artifacts) {
    const sourceFile = `${buildDir}/zephyr/zephyr.${fileExt}`;
    const targetFile = `${outputDir}/${boardName}.${fileExt}`;

    try {
      await Deno.copyFile(sourceFile, targetFile);
      console.log(`📁 Copied: ${sourceFile} -> ${targetFile}\n`);
    } catch (error) {
      if (error instanceof Deno.errors.NotFound) {
        console.log(`⚠️  File not found: ${sourceFile}\n`);
      } else {
        throw error;
      }
    }
  }
}

async function run() {
  await removeDirectory(buildDir);
  await Deno.mkdir(outputDir, { recursive: true });

  for (const board of boards) {
    const args = [
      "build",
      "-p",
      "always",
      "-b",
      board.target,
      "--build-dir",
      buildDir,
      ...(board.westArgs ?? []),
      "--",
      ...board.features.map((feature) => `-DCONFIG_OSKEY_${feature}=y`),
      ...(board.overlays
        ? [`-DEXTRA_DTC_OVERLAY_FILE=${board.overlays.join(";")}`]
        : []),
      ...(board.cmakeArgs ?? []),
    ];

    console.log(`🔨 Build: ${board.name}\n`);
    console.log(`🔨 Command: west ${args.join(" ")}\n`);

    const { code } = await new Deno.Command("west", {
      args,
      stdout: "inherit",
      stderr: "inherit",
    }).output();

    if (code !== 0) {
      console.error(`❌ ${board.name} build failed with exit code ${code}\n`);
      await removeDirectory(buildDir);
      Deno.exit(code);
    }

    console.log(`✅ ${board.name} build succeeded\n`);
    await copyBuildFiles(board.name);
  }

  await removeDirectory(buildDir);
}

console.log("\n🚀 Start building...\n");
await run();
console.log("\n🎉 All builds completed successfully!\n");
