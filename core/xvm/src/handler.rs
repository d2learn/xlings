use std::fs;
use std::process::exit;

use clap::ArgMatches;
use anyhow::{Result, Context};
use colored::*;

use xvmlib::shims;
use xvmlib::Workspace;

use crate::cmdprocessor;
use crate::baseinfo;
use crate::helper;

pub fn xvm_add(matches: &ArgMatches, _cmd_state: &cmdprocessor::CommandState) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let version = matches.get_one::<String>("version").context("Version is required")?;
    let path = matches.get_one::<String>("path");
    let alias = matches.get_one::<String>("alias");
    let vtype = matches.get_one::<String>("type");
    let filename = matches.get_one::<String>("filename");

    let env_vars: Vec<String> = matches
        .get_many::<String>("env")
        .unwrap_or_default()
        .map(|s| s.to_string())
        .collect();

    println!("adding target: {}, version: {}", target.green().bold(), version.cyan());

    let mut program = shims::Program::new(target, version);
    let mut vdb = xvmlib::get_versiondb().clone();

    if vdb.is_empty(target) {
        println!("set [{} {}] as default", target.green().bold(), version.cyan());
        let mut workspace = xvmlib::get_global_workspace().clone();
        workspace.set_version(target, version);
        workspace.save_to_local().context("Failed to save Workspace")?;
    } else {
        // set type and filename by vdb
        let vtype_tmp = vdb.get_type(target).cloned();
        if vtype_tmp.is_some() {
            program.set_type(&vtype_tmp.clone().unwrap());
        }
        let filename_tmp = vdb.get_filename(target).cloned();
        if filename_tmp.is_some() {
            program.set_filename(&filename_tmp.clone().unwrap());
        }
    }

    if let Some(t) = vtype {
        program.set_type(t);
        vdb.set_type(target, t);
    }

    if let Some(f) = filename {
        program.set_filename(f);
        vdb.set_filename(target, f);
    }

    if let Some(p) = path {
        program.set_path(p);
    }

    if let Some(c) = alias {
        program.set_alias(c);
    }

    if !env_vars.is_empty() {
        //println!("Environment variables: {:?}", env_vars);
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

    vdb.set_vdata(target, version, program.vdata());
    vdb.save_to_local().context("Failed to save VersionDB")?;

    // if type is lib, create a link in libdir
    if vtype.is_some_and(|t| t == "lib") {
        let libdir = baseinfo::libdir();
        println!("link [{} {}] to [{}] ...", target, version, libdir.bright_purple());
        program.link_to(&libdir, false);
    } else {
        // create a bin shim
        xvmlib::shims::try_create(target, &baseinfo::bindir());
    }

    Ok(())
}

pub fn xvm_remove(matches: &ArgMatches, _cmd_state: &cmdprocessor::CommandState) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;

    // If no version is provided, remove all versions
    let version = matches.get_one::<String>("version");
    let mut vdb = xvmlib::get_versiondb().clone();
    let mut workspace = xvmlib::get_global_workspace().clone();
    let workspace_version = workspace.version(target);
    let mut global_version_removed = false;

    let mut vtype: Option<String> = Option::None;
    let mut libname: Option<String> = Option::None;

    if vdb.has_target(target) {
        libname = vdb.get_filename(target).cloned();
        vtype = vdb.get_type(target).cloned();
    }

    if version.is_none() { // 检查 version 是否为 None
        if !_cmd_state.yes && !helper::prompt(&format!("remove all versions for [{}]? (y/n): ", target.green().bold()), "y") {
            return Ok(());
        }
        println!("removing...");
        vdb.remove_all_vdata(target);
    } else {
        let version = version.unwrap();
        if !vdb.has_version(target, &version) {
            println!("[{} {}] not found in the xvm database, or already removed",
                target.yellow(),
                version.yellow()
            );
            return Ok(());
        }

        println!("removing target: {}, version: {}", target.green().bold(), version.cyan());
        vdb.remove_vdata(target, version);
        // if removed version is current version, set update flag
        if workspace_version == Some(version) {
            global_version_removed = true;
        }
    }

    vdb.save_to_local().context("Failed to save VersionDB")?;

    // only update global workspace - system config file
    // local workspace update by user - user config file
    if vdb.is_empty(target) { // if is empty delete from workspace
        workspace.remove(target);
        println!("remove [{}] from [{}] workspace", target.green().bold(), "global".bold().bright_purple());
        // if type is lib, remove link from libdir
        if vtype.is_some_and(|t| t == "lib") {
            let libdir = baseinfo::libdir();
            // TODO: to support windows
            let lib_path = format!("{}/{}", libdir, libname.unwrap_or_else(|| format!("{}.so", target)));
            if fs::symlink_metadata(&lib_path).is_ok() {
                fs::remove_file(&lib_path).unwrap();
            }
            println!("remove link [{}] - {} ...", target.green().bold(), lib_path);
        } else {
            // remove bin shim
            xvmlib::shims::delete(target, &baseinfo::bindir());
            println!("remove shim [{}] ...", target.green().bold());
        }
        workspace.save_to_local().context("Failed to save Workspace")?;
    } else if global_version_removed {
        let first_version = vdb.get_first_version(target).unwrap();
        workspace.set_version(target, first_version);
        println!("set [{} {}] as default", target.green().bold(), first_version.cyan());
        // if is lib, relink
        if vtype.is_some_and(|t| t == "lib") {
            let mut program = shims::Program::new(target, first_version);
            let vdata = vdb
                .get_vdata(target, first_version)
                .unwrap_or_else(|| {
                    println!("[{} {}] not found in the xvm database",
                        target.red(),
                        first_version.red()
                    );
                    std::process::exit(1);
                });

            program.set_filename(vdb.get_filename(target).unwrap());
            program.set_vdata(vdata);
            let libdir = baseinfo::libdir();
            println!("relink [{} {}] to [{}] ...", target.green().bold(), first_version.cyan(), libdir.bright_purple());
            program.link_to(&libdir, true);
        }
        workspace.save_to_local().context("Failed to save Workspace")?;
    } else {
        // vdb not empty and global version not removed
    }

    Ok(())
}

pub fn xvm_use(matches: &ArgMatches, _cmd_state: &cmdprocessor::CommandState) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let mut version = matches.get_one::<String>("version").context("Version is required")?;

    let vdb = xvmlib::get_versiondb();

    if !vdb.has_target(target) {
        println!("Target [{}] is missing from the xvm database", target.bold().red());
        println!("\n\t{}\n", "xvm add [target] [version] ...".yellow());
        std::process::exit(1);
    }

    if !vdb.has_version(target, &version) {
        version = vdb.match_first_version(target, &version).unwrap_or_else(|| {
            println!("[{} {}] not found in the xvm database",
                target.yellow(),
                version.yellow()
            );
            println!("\n\t[{} {}]\n", "xvm list".bold().cyan(), target.bold().cyan());
            let matches = cmdprocessor::parse_from_string(&["xvm", "list", target]);
            cmdprocessor::run(&matches).unwrap();
            std::process::exit(1);
        });
    }

    let mut workspace = helper::load_workspace();

    if workspace.version(target) != Some(version) {
        // if type is lib, relink
        if let Some(vdata) = vdb.get_vdata(target, version) {
            if vdb.get_type(target) == Some(&"lib".to_string()) {
                let libdir = baseinfo::libdir();
                println!("relink [{} {}] to [{}] ...", target.green().bold(), version.cyan(), libdir.bright_purple());
                let mut program = shims::Program::new(target, version);
                program.set_filename(vdb.get_filename(target).unwrap());
                program.set_vdata(vdata);
                program.link_to(&libdir, true);
            }
        }
        workspace.set_version(target, version);
        workspace.save_to_local().context("Failed to save Workspace")?;
    }

    println!("using -> target: {}, version: {}", target.green().bold(), version.cyan());

    Ok(())
}

pub fn xvm_current(matches: &ArgMatches, _cmd_state: &cmdprocessor::CommandState) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;

    let workspace = helper::load_workspace_and_merge();
    let name_version_pairs = workspace.match_by(target);
    let vdb = xvmlib::get_versiondb();

    let mut installed_num : u32 = 0;
    let total_num = name_version_pairs.len() as u32;

    // workspace title
    println!("\n\t[[{}]]\n", workspace.name().bold().bright_purple());

    for (name, version) in name_version_pairs {
        print!("{}:\t", name.bold(), );
        if vdb.has_version(name, &version) {
            installed_num += 1;
            print!("{}", version.cyan());
            let vdata = vdb.get_vdata(name, &version).unwrap();
            if let Some(alias) = &vdata.alias {
                print!("\t -->  [{}]", alias.dimmed());
            }
        } else {
            print!("{}", version.red());
        }
        println!();
    }

    if total_num == 0 {
        println!("{} not found in [{}] workspace",
            target.bold().red(), workspace.name().bold().bright_purple()
        );
    } else {
        println!();
        if installed_num == total_num {
            println!("all targets added");
        } else {
            println!("{} of {} targets added", installed_num, total_num);
        }
        println!();
    }

    Ok(())
}

pub fn xvm_run(matches: &ArgMatches, _cmd_state: &cmdprocessor::CommandState) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let args: Vec<String> = matches
            .get_many::<String>("args")
            .unwrap_or_default()
            .map(|s| s.to_string())
            .collect();

    let workspace = helper::load_workspace_and_merge();
    let version = matches.get_one::<String>("version").unwrap_or_else(|| {
        workspace.version(target).unwrap_or_else(|| {
            println!(
                "{} not found in [{}] workspace",
                target.bold().red(), workspace.name().bold().bright_purple()
            );
            std::process::exit(1);
        })
    });

    let mut program = shims::Program::new(target, version);
    let vdb = xvmlib::get_versiondb();
    let vdata = vdb
        .get_vdata(target, version)
        .unwrap_or_else(|| {
            println!("[{} {}] not found in the xvm database",
                target.yellow(),
                version.yellow()
            );
            std::process::exit(1);
        });

    program.set_vdata(vdata);

    if !args.is_empty() {
        program.add_args(&args);
    }

    exit(program.run());
}

pub fn xvm_list(matches: &ArgMatches, _cmd_state: &cmdprocessor::CommandState) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;

    let vdb = xvmlib::get_versiondb();
    let workspace = helper::load_workspace_and_merge();

    let versions_print = |template: &str, tv: (&str, Vec<String>)| {
        let (target, versions) = tv;
        let current_version = workspace.version(target);

        for version in versions {
            let formatted_version = if current_version.as_deref() == Some(&version) {
                format!("{}", version.cyan().bold())
            } else {
                format!("{}", version)
            };

            let output = template.replace("{}", &formatted_version);

            println!("{}", output);
        }
    };

    if vdb.has_target(target) {
        let versions = vdb.get_all_version(target).unwrap_or_default();
        versions_print("{}", (&target, versions));
    } else { // print all matched targets
        let matches = vdb.match_by(target);

        for (name, versions) in matches {
            println!("{}:", name.bold());
            versions_print("  {}", (&name, versions));
        }
    }

    println!();

    Ok(())
}

pub fn xvm_workspace(matches: &ArgMatches, _cmd_state: &cmdprocessor::CommandState) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let active = matches.get_one::<bool>("active");
    let inherit = matches.get_one::<bool>("inherit");

    let mut need_save = false;

    let mut workspace = if target == "global" {
        xvmlib::get_global_workspace().clone()
    } else {
        if fs::metadata(baseinfo::WORKSPACE_FILE).is_ok() {
            helper::load_local_workspace()
        } else {
            println!("\n\t[ {} ]\n", target.bright_purple().bold());
            if helper::prompt("create workspace? (y/n): ", "y") {
                need_save = true;
                Workspace::new(baseinfo::WORKSPACE_FILE, target)
            } else {
                return Ok(());
            }
        }
    };

    if target != workspace.name() {
        println!("\n\t[  {}  ->  {}  ]\n",
            workspace.name().purple().bold().dimmed(),
            target.bright_purple().bold()
        );
        if helper::prompt("rename workspace? (y/n): ", "y") {
            workspace.set_name(target);
            println!("set workspace name to [{}] - ok", target.bold().bright_purple());
            need_save = true;
        } else {
            return Ok(());
        }
    }

    if let Some(active) = active {
        println!("set workspace [{}] - active: {}", target.bold().bright_purple(), active);
        workspace.set_active(active);
        need_save = true;
        if workspace.name() == "global" {
            if *active {
                // restore all shims
                println!("restore all shims ...\n");
                for (target, _) in workspace.all_versions() {
                    println!("{} {}", "+".green().bold(), target);
                    shims::try_create(target, &baseinfo::bindir());
                }
            } else {
                // remove all shims
                println!("remove all shims ...\n");
                for (target, _) in workspace.all_versions() {
                    println!("{} {}", "-".red().bold(), target);
                    shims::delete(target, &baseinfo::bindir());
                }
            }
            println!("\ndone.\n");
        }
    }

    if let Some(inherit) = inherit {
        println!("set workspace [{}] - inherit: {}", target.bold().bright_purple(), inherit);
        workspace.set_inherit(inherit);
        need_save = true;
    }

    if need_save {
        workspace.save_to_local().context("Failed to save Workspace")?;
        println!("update workspace [{}] - ok", target.bold().bright_purple());
    }

    Ok(())
}