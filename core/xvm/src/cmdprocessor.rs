use std::fs;

use clap::{Arg, ArgAction, Command, ArgMatches};
use anyhow::{Result, Context};

use xvmlib::shims;
use xvmlib::Workspace;

use crate::baseinfo;
use crate::helper;

pub fn run() -> Result<()> {
    let matches = Command::new("xvm")
    .version("prev-0.0.2")
    .author("d2learn <dev@d2learn.com>")
    .about("a simple and generic version management tool")
    .subcommand(
        Command::new("add")
            .about("Add a target")
            .arg(
                Arg::new("target")
                    .required(true)
                    .help("The name of the target"),
            )
            .arg(
                Arg::new("version")
                    .required(true)
                    .help("The version of the target"),
            )
            .arg(
                Arg::new("path")
                    .long("path")
                    .value_name("PATH")
                    .action(ArgAction::Set)
                    .help("Specify the installation path for the target"),
            )
            .arg(
                Arg::new("filename")
                    .long("filename")
                    .value_name("FILENAME")
                    .action(ArgAction::Set)
                    .help("Specify a filename for the target"),
            )
            .arg(
                Arg::new("env")
                    .long("env")
                    .value_name("ENV")
                    .action(ArgAction::Append)
                    .help("Set environment variables for the target (format: name=value)"),
            ),
    )
    .subcommand(
        Command::new("remove")
            .about("Remove a target")
            .arg(
                Arg::new("target")
                    .required(true)
                    .help("The name of the target"),
            )
            .arg(
                Arg::new("version")
                    .required(true)
                    .help("The version of the target"),
            ),
    )
    .subcommand(
        Command::new("use")
            .about("Use a specific target and version")
            .arg(
                Arg::new("target")
                    .required(true)
                    .help("The name of the target"),
            )
            .arg(
                Arg::new("version")
                    .required(true)
                    .help("The version of the target"),
            ),
    )
    .subcommand(
        Command::new("current")
            .about("Show the current target's version")
            .arg(
                Arg::new("target")
                    .required(true)
                    .help("The name of the target"),
            ),
    )
    .subcommand(
        Command::new("list")
            .about("List all versions for a target")
            .arg(
                Arg::new("target")
                    .required(true)
                    .help("The name of the target"),
            ),
    )
    .subcommand(
        Command::new("run")
            .about("Run a target program")
            .arg(
                Arg::new("target")
                    .required(true)
                    .help("The name of the target"),
            )
            .arg(
                Arg::new("version")
                    //.required(true)
                    .help("The version of the target"),
            )
            .arg(
                Arg::new("args")
                    //.required(true)
                    .long("args")
                    //.value_name("ARGS")
                    .action(ArgAction::Set)
                    .num_args(1..)
                    .allow_hyphen_values(true)
                    .help("Arguments to pass to the command")
            ),
    )
    .subcommand(
        Command::new("workspace")
            .about("Manage xvm's workspaces")
            .arg(
                Arg::new("target")
                    .required(true)
                    .help("The name of the target"),
            )
            .arg(
                Arg::new("enable")
                    .long("enable")
                    .action(ArgAction::SetTrue)
                    .help("Enable the workspace"),
            )
            .arg(
                Arg::new("disable")
                    .long("disable")
                    .action(ArgAction::SetTrue)
                    .help("Disable the workspace"),
            ),
    )
    .get_matches();

    match matches.subcommand() {
        Some(("add", sub_matches)) => handle_add(sub_matches)?,
        Some(("remove", sub_matches)) => handle_remove(sub_matches)?,
        Some(("use", sub_matches)) => handle_use(sub_matches)?,
        Some(("current", sub_matches)) => handle_current(sub_matches)?,
        Some(("run", sub_matches)) => handle_run(sub_matches)?,
        Some(("list", sub_matches)) => handle_list(sub_matches)?,
        Some(("workspace", sub_matches)) => handle_workspace(sub_matches)?,
        _ => println!("Unknown command. Use --help for usage information."),
    }

    Ok(())
}

fn handle_add(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let version = matches.get_one::<String>("version").context("Version is required")?;
    let path = matches.get_one::<String>("path");
    let filename = matches.get_one::<String>("filename");
    let env_vars: Vec<String> = matches
        .get_many::<String>("env")
        .unwrap_or_default()
        .map(|s| s.to_string())
        .collect();

    println!("Adding target: {}, version: {}", target, version);

    let mut program = shims::Program::new(target, version);

    if let Some(p) = path {
        println!("Path: {}", p);
        program.set_path(p);
    }

    if let Some(f) = filename {
        println!("Filename: {}", f);
        program.set_filename(f);
    }

    if !env_vars.is_empty() {
        println!("Environment variables: {:?}", env_vars);
        program.add_envs(
            &env_vars
                .iter()
                .map(|s| {
                    let parts: Vec<&str> = s.split('=').collect();
                    (parts[0], parts[1])
                })
                .collect::<Vec<_>>(),
        );
    }

    let mut vdb = xvmlib::get_versiondb().clone();

    if vdb.is_empty(target) {
        xvmlib::shims::create(target, baseinfo::BINDIR);
    }

    vdb.set_vdata(target, version, program.vdata());

    vdb.save_to_local().context("Failed to save VersionDB")?;

    Ok(())
}

fn handle_remove(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let version = matches.get_one::<String>("version").context("Version is required")?;
    println!("Removing target: {}, version: {}", target, version);

    let mut vdb = xvmlib::get_versiondb().clone();
    vdb.remove_vdata(target, version);
    vdb.save_to_local().context("Failed to save VersionDB")?;

    if vdb.is_empty(target) {
        xvmlib::shims::delete(target, baseinfo::BINDIR);
    }

    Ok(())
}

fn handle_use(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let version = matches.get_one::<String>("version").context("Version is required")?;
    println!("Using target: {}, version: {}", target, version);

    let vdb = xvmlib::get_versiondb();

    if vdb.get_vdata(target, version).is_none() {
        panic!("Version not found");
    }

    let mut workspace = helper::get_workspace();

    if workspace.version(target) != Some(version) {
        workspace.set_version(target, version);
        workspace.save_to_local().context("Failed to save Workspace")?;
    }

    Ok(())
}

fn handle_current(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let workspace = helper::get_workspace();

    if let Some(version) = workspace.version(target) {
        println!("{}: {}", target, version);
    } else {
        println!("No version selected");
    }

    Ok(())
}

fn handle_run(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let args: Vec<String> = matches
            .get_many::<String>("args")
            .unwrap_or_default()
            .map(|s| s.to_string())
            .collect();

    let workspace = helper::get_workspace(); // TODO: optimize
    let version = matches.get_one::<String>("version").unwrap_or_else(|| {
        workspace.version(target).expect("No version selected")
    });

    let mut program = shims::Program::new(target, version);
    let vdb = xvmlib::get_versiondb();
    let vdata = vdb
        .get_vdata(target, version)
        .expect("Version data not found");

    program.set_vdata(vdata);

    if !args.is_empty() {
        program.add_args(&args);
    }

    program.run();

    Ok(())
}

fn handle_list(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;

    let vdb = xvmlib::get_versiondb();
    let versions = vdb.get_all_version(target).unwrap_or_default();

    for version in versions {
        println!("\t{}", version);
    }

    Ok(())
}

fn handle_workspace(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let enable = matches.get_flag("enable");
    let disable = matches.get_flag("disable");

    let mut workspace = if target == "global" {
        xvmlib::get_global_workspace().clone()
    } else {
        if fs::metadata("workspace.xvm.yaml").is_ok() {
            helper::get_local_workspace()
        } else {
            println!("create new workspace [{}]", target);
            Workspace::new("workspace.xvm.yaml", target)
        }
    };

    if enable {
        println!("active workspace [{}]", target);
        workspace.set_active(true);
    }
    if disable {
        println!("deactive workspace [{}]", target);
        workspace.set_active(false);
    }

    workspace.save_to_local().context("Failed to save Workspace")?;

    Ok(())
}