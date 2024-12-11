# Copyright (c) 2024 Northern.tech
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import pathlib
import subprocess
import sys

# Read the docs: https://docs.zephyrproject.org/latest/develop/west/west-apis.html
from west.commands import WestCommand

# TODO Get ZEPHYR_BASE from env, or zephyr_ext_common, or somewhere...
# ZEPHYR_BASE = os.environ['ZEPHYR_BASE']
# from zephyr_ext_common import ZEPHYR_BASE

ZEPHYR_BASE = "/home/lluis/west/zephyr"
sys.path.insert(0, os.path.join(ZEPHYR_BASE, "scripts", "west_commands"))

from build_helpers import find_build_dir, is_zephyr_build, FIND_BUILD_DIR_DESCRIPTION
from runners.core import BuildConfiguration


class MenderCommands(WestCommand):
    def __init__(self):
        super().__init__(
            "mender",
            # Keep this in sync with the string in west-commands.yml.
            "execute Mender commands: artifact, upload",
            "Generate the Mender Artifact or upload the Mender Artifact to the Mender Server",
            accepts_unknown_args=False,
        )

    def add_common_arguments(self, parser):
        parser.add_argument(
            "-n", "--artifact-name", help="Name of the artifact",
        )

        # TODO: accept multiple device types
        parser.add_argument(
            "--device-type",
            help="Type of device(s) supported by the Artifact. If not specified, uses Kconfig BOARD configuration",
        )
        parser.add_argument(
            "-T",
            "--type",
            help="Name of the update module. Defaults to 'zephyr-image'",
            default="zephyr-image",
        )

        parser.add_argument(
            "--artifact-filename", help="Artifact filename",
        )

    def do_add_parser(self, parser_adder):
        self.parent_parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
        )

        # TODO: domain ?

        self.parent_parser.add_argument(
            "-q", "--quiet", action="store_true", help="suppress non-error output"
        )

        self.parent_parser.add_argument(
            "-d", "--build-dir", help=FIND_BUILD_DIR_DESCRIPTION
        )

        subparsers = self.parent_parser.add_subparsers(
            title="subcommand", dest="subcommand", required=True
        )

        parser_artifact = subparsers.add_parser("artifact")
        self.add_common_arguments(parser_artifact)

        parser_upload = subparsers.add_parser("upload")
        parser_upload.add_argument(
            "--server",
            help="Mender Server URL. Defaults to 'https://hosted.mender.io'",
            default="https://hosted.mender.io",
        )
        self.add_common_arguments(parser_upload)

        return self.parent_parser

    def do_run(self, args, _):
        # TODO: explain the hack...
        filtered_args = []
        for arg in sys.argv[1:]:
            if arg != "mender":
                filtered_args.append(arg)
        parent_args, _ = self.parent_parser.parse_known_args(filtered_args)

        if parent_args.subcommand == "artifact":
            self.do_mender_artifact(args)
        elif parent_args.subcommand == "upload":
            self.do_mender_upload(args)
        else:
            self.die(f"Unrecognized mender subcommand {args.subcommand}")

    def default_device_type(self, build_dir):
        build_conf = BuildConfiguration(build_dir)
        config_board = build_conf.get("CONFIG_BOARD")
        if config_board == "":
            self.die("Cannot find CONFIG_BOARD and no device_type was specified")
        return config_board

    def default_artifact_path(self, build_dir, module_type, device_type, artifact_name):
        return (
            pathlib.Path(build_dir)
            / f"{module_type}-{device_type}-{artifact_name}.mender"
        )

    def do_mender_artifact(self, args):
        # TODO: figure out mender-artifact path

        # TODO
        # Check for --artifact-name

        # TODO: refactor the build_dir checks
        build_dir = find_build_dir(args.build_dir)
        if not os.path.isdir(build_dir):
            self.die(f"no such build directory {build_dir}")
        if not is_zephyr_build(build_dir):
            self.die(
                f"build directory {build_dir} doesn't look like a Zephyr build "
                "directory"
            )
        self.dbg(f"Build directory is {build_dir}")
        build_conf = BuildConfiguration(build_dir)

        config_bin = build_conf.getboolean("CONFIG_BUILD_OUTPUT_BIN")
        if not config_bin:
            self.die(f"artifact command only works with CONFIG_BUILD_OUTPUT_BIN option")

        config_kernel = build_conf.get("CONFIG_KERNEL_BIN_NAME", "zephyr")

        zephyr_signed_bin = pathlib.Path(build_dir) / "zephyr" / f"{config_kernel}.bin"
        if not zephyr_signed_bin.is_file():
            self.die(f"no unsigned .bin found at {zephyr_signed_bin}")
        self.dbg(f"Signed binary is {zephyr_signed_bin}")

        device_type = args.device_type
        if not args.device_type:
            device_type = self.default_device_type(build_dir)
        self.dbg(f"Using device-type {device_type}")

        output_path = args.artifact_filename
        if not args.artifact_filename:
            output_path = self.default_artifact_path(
                build_dir, args.type, device_type, args.artifact_name
            )
        self.dbg(f"Writing Artifact into {output_path}")

        mender_artifact_cmd = [
            "mender-artifact",
            "write",
            "module-image",
            "--type",
            args.type,
            "--artifact-name",
            args.artifact_name,
            "--compression",
            "none",
            "--device-type",
            device_type,
            "--file",
            str(zephyr_signed_bin),
            "--output-path",
            str(output_path),
        ]

        self.dbg(f"Calling command {str(mender_artifact_cmd)}")

        # TODO: review stdout pipe
        self.check_call(
            mender_artifact_cmd, stdout=subprocess.PIPE if args.quiet else None
        )

        self.inf(rf"Success \o/ Mender Artifact generated at {str(output_path)}")

    def do_mender_upload(self, args):
        # TODO: figure out mender-cli path

        # TODO
        # Check for either --artifact-filename or all the others

        # TODO: check another API before to conditionally do login
        mender_cli_login_cmd = [
            "mender-cli",
            "login",
            "--server",
            args.server,
        ]
        self.check_call(
            mender_cli_login_cmd, stdout=subprocess.PIPE if args.quiet else None
        )

        build_dir = find_build_dir(args.build_dir)
        if not os.path.isdir(build_dir):
            self.die(f"no such build directory {build_dir}")
        if not is_zephyr_build(build_dir):
            self.die(
                f"build directory {build_dir} doesn't look like a Zephyr build "
                "directory"
            )

        device_type = args.device_type
        if not args.device_type:
            device_type = self.default_device_type(build_dir)
        self.dbg(f"Using device-type {device_type}")

        artifact = args.artifact_filename
        if not args.artifact_filename:
            artifact = self.default_artifact_path(
                build_dir, args.type, device_type, args.artifact_name
            )

        mender_cli_upload_cmd = [
            "mender-cli",
            "artifacts",
            "upload",
            str(artifact),
        ]
        self.check_call(
            mender_cli_upload_cmd, stdout=subprocess.PIPE if args.quiet else None
        )
