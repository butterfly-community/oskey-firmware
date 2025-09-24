#!/usr/bin/env -S deno run --allow-read --allow-write --allow-run --allow-env

const config = {
  command:
    'west build -p always -b {board} --build-dir temp {command} -- -DEXTRA_CONF_FILE="{conf}" -DEXTRA_DTC_OVERLAY_FILE="{overlay}" {extra}',
  files: ["bin", "elf", "uf2"],
  boards: [
    {
      name: "stm32_nucleo_f401re",
      target: "nucleo_f401re",
      conf: ["boards/conf/enable_test_rng.conf"],
      overlay: [],
      extra: "-D CONFIG_HEAP_MEM_POOL_SIZE=40960"
    },
    {
      name: "esp32c2_devkitm",
      target: "esp8684_devkitm",
      conf: ["boards/conf/enable_storage.conf"],
      overlay: [],
    },
    {
      name: "esp32c3_devkitm",
      target: "esp32c3_devkitm",
      conf: ["boards/conf/enable_storage.conf"],
      overlay: [],
    },
    {
      name: "esp32c3_core",
      target: "esp32c3_devkitm",
      conf: ["boards/conf/enable_storage.conf"],
      overlay: ["boards/overlay/esp32_usb_jtag_serial.overlay"],
    },
    {
      name: "esp32s3_devkitm",
      target: "esp32s3_devkitm/esp32s3/procpu",
      conf: ["boards/conf/enable_storage.conf"],
      overlay: [],
    },
    {
      name: "esp32s3_core",
      target: "esp32s3_devkitm/esp32s3/procpu",
      conf: ["boards/conf/enable_storage.conf"],
      overlay: ["boards/overlay/esp32_usb_jtag_serial.overlay"],
    },
    {
      name: "lichuang_szpi_s3",
      target: "esp32s3_devkitm/esp32s3/procpu",
      conf: [
        "boards/conf/enable_storage.conf",
        "boards/conf/enable_lvgl.conf",
        "boards/esp32s3_lichuang.conf",
      ],
      overlay: ["boards/esp32s3_lichuang.overlay"],
    },
    {
      name: "lichuang_szpi_s3_usb_jtag_serial",
      target: "esp32s3_devkitm/esp32s3/procpu",
      conf: [
        "boards/conf/enable_storage.conf",
        "boards/conf/enable_lvgl.conf",
        "boards/esp32s3_lichuang.conf",
      ],
      overlay: ["boards/esp32s3_lichuang.overlay", "boards/overlay/esp32_usb_jtag_serial.overlay"],
    },
    {
      name: "stm32h747i_disco",
      target: "stm32h747i_disco/stm32h747xx/m7",
      conf: ["boards/conf/enable_storage.conf", "boards/conf/enable_lvgl.conf"],
      overlay: [],
      command: "--shield st_b_lcd40_dsi1_mb1166",
    },
    {
      name: "stm32f769i_disco",
      target: "stm32f769i_disco",
      conf: ["boards/conf/enable_storage.conf"],
      overlay: ["boards/overlay/stm32_rng.overlay"],
    },
    {
      name: "nrf52840_mdk",
      target: "nrf52840_mdk",
      conf: ["boards/conf/enable_storage.conf"],
      overlay: [],
    },
    {
      name: "rpi_pico",
      target: "rpi_pico",
      conf: ["boards/conf/enable_test_rng.conf"],
      overlay: [],
    },
  ],
};

console.log("\n🚀 Start Buiding...\n");

async function run() {
  await cleanTemp();

  for (const board of config.boards) {
    const command = config.command
      .replace("{board}", board.target)
      .replace("{conf}", board.conf.join(";"))
      .replace("{overlay}", board.overlay.join(";"))
      .replace("{command}", board.command ?? "")
      .replace("{extra}", board.extra ?? "");

    console.log(`🔨 Build: ${board.name}\n`);

    console.log(`🔨 Command: ${command}\n`);

    const shell = Deno.env.get("SHELL") || "bash";

    const process = new Deno.Command(shell, {
      args: ["-c", command],
      stdout: "inherit",
      stderr: "inherit",
    });

    const { code } = await process.output();

    if (code !== 0) {
      console.error(`❌ ${board.name} Build failed with exit code ${code}\n`);
      Deno.exit(code);
    }
    console.log(`✅ ${board.name} Build succeeded\n`);

    const execFileOwner = new Deno.Command(shell, {
      args: ["-c", "chmod -R 777 boards/build"],
      stdout: "inherit",
      stderr: "inherit",
    });

    await execFileOwner.output();

    await copyBuildFiles(board.name);
  }

  await cleanTemp();
}

async function cleanTemp() {
  const shell = Deno.env.get("SHELL") || "bash";

  const cleanProcess = new Deno.Command(shell, {
    args: ["-c", "rm -rf temp"],
    stdout: "inherit",
    stderr: "inherit",
  });

  await cleanProcess.output();
}

async function copyBuildFiles(boardName: string) {
  await Deno.mkdir("boards/build", { recursive: true });

  for (const fileExt of config.files) {
    const sourceFile = `temp/zephyr/zephyr.${fileExt}`;
    const targetFile = `boards/build/${boardName}.${fileExt}`;

    try {
      await Deno.stat(sourceFile);
      await Deno.copyFile(sourceFile, targetFile);
      console.log(`📁 Copied: ${sourceFile} -> ${targetFile}\n`);
    } catch (error) {
      if (error instanceof Deno.errors.NotFound) {
        console.log(`⚠️  File not found: ${sourceFile}\n`);
      } else {
        console.error(`❌ Error copying ${sourceFile}:`, error);
      }
    }
  }
}

await run();

console.log("\n🎉 All builds completed successfully!\n");

console.log("Done! 🎉\n");
