#
# This file is the default set of rules to compile a Pebble project.
#
# Feel free to customize this to your needs.
#

import os.path

top = "."
out = "build"


def options(ctx):
    ctx.load("pebble_sdk")


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load("pebble_sdk")


def build(ctx):
    ctx.add_group("rust")
    ctx.set_group("rust")
    rust_build = ctx(rule="cargo build --release", always=True)

    ctx.load("pebble_sdk")
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        ctx.env.append_value(
            "LINKFLAGS",
            [
                "-Wl,--exclude-libs,libgcc.a",
                "-Wl,--defsym=__exidx_start=0",
                "-Wl,--defsym=__exidx_end=0",
            ],
        )

        ctx.env.append_value(
            "STLIBPATH",
            [ctx.path.abspath() + "/target/thumbv8m.main-none-eabi/release/"],
        )
        ctx.env.append_value("STLIB", ["pebble_64cores"])

        app_elf = "{}/pebble-app.elf".format(ctx.env.BUILD_DIR)

        ctx.pbl_program(
            source=[],
            target=app_elf,
        )

        binaries.append({"platform": platform, "app_elf": app_elf})

    ctx.env = cached_env

    ctx.set_group("bundle")
    ctx.pbl_bundle(
        binaries=binaries,
        js=ctx.path.ant_glob(
            ["src/pkjs/**/*.js", "src/pkjs/**/*.json", "src/common/**/*.js"]
        ),
        js_entry_file="src/pkjs/index.js",
    )
