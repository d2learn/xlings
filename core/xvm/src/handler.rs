use std::fs;

use clap::ArgMatches;
use anyhow::{Result, Context};
use colored::*;

use xvmlib::shims;
use xvmlib::Workspace;

use crate::cmdprocessor;
use crate::baseinfo;
use crate::helper;

pub fn xvm_add(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let version = matches.get_one::<String>("version").context("Version is required")?;
    let path = matches.get_one::<String>("path");
    let alias = matches.get_one::<String>("alias");
    let env_vars: Vec<String> = matches
        .get_many::<String>("env")
        .unwrap_or_default()
        .map(|s| s.to_string())
        .collect();

    println!("adding target: {}, version: {}", target.green().bold(), version.cyan());

    let mut program = shims::Program::new(target, version);

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

    let mut vdb = xvmlib::get_versiondb().clone();

    if vdb.is_empty(target) {
        println!("set [{} {}] as default", target.green().bold(), version.cyan());
        let mut workspace = xvmlib::get_global_workspace().clone();
        workspace.set_version(target, version);
        workspace.save_to_local().context("Failed to save Workspace")?;
    }

    vdb.set_vdata(target, version, program.vdata());
    vdb.save_to_local().context("Failed to save VersionDB")?;

    xvmlib::shims::try_create(target, &baseinfo::bindir());

    Ok(())
}

pub fn xvm_remove(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;

    // If no version is provided, remove all versions
    let version = matches.get_one::<String>("version");
    let mut vdb = xvmlib::get_versiondb().clone();

    if version.is_none() { // 检查 version 是否为 None
        helper::prompt(&format!("remove all versions for [{}]? (y/n): ", target.green().bold()), "y");
        println!("removing...");
        vdb.remove_all_vdata(target);
    } else {
        let version = version.unwrap();
        println!("removing target: {}, version: {}", target.green().bold(), version.cyan());
        vdb.remove_vdata(target, version);
    }

    vdb.save_to_local().context("Failed to save VersionDB")?;

    if vdb.is_empty(target) {
        let mut workspace = xvmlib::get_global_workspace().clone();
        workspace.remove(target);
        workspace.save_to_local().context("Failed to save Workspace")?;
        println!("remove [{}] from workspace", target.green().bold());
        xvmlib::shims::delete(target, &baseinfo::platform::bindir());
        println!("delete shim [{}] ...", target.green().bold());
    }

    Ok(())
}

pub fn xvm_use(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let mut version = matches.get_one::<String>("version").context("Version is required")?;

    let vdb = xvmlib::get_versiondb();

    if !vdb.has_target(target) {
        println!("Target [{}] is missing from the xvm database. Please add it before proceeding.", target);
        std::process::exit(1);
    }

    if !vdb.has_version(target, &version) {
        version = vdb.match_first_version(target, &version).unwrap_or_else(|| {
            println!("{}\n", "try to use a correct version...".yellow());
            let matches = cmdprocessor::parse_from_string(&["xvm", "list", target]);
            cmdprocessor::run(&matches).unwrap();
            std::process::exit(1);
        });
    }

    let mut workspace = helper::load_workspace();

    if workspace.version(target) != Some(version) {
        workspace.set_version(target, version);
        workspace.save_to_local().context("Failed to save Workspace")?;
    }

    println!("using -> target: {}, version: {}", target.green().bold(), version.cyan());

    Ok(())
}

pub fn xvm_current(matches: &ArgMatches) -> Result<()> {
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

pub fn xvm_run(matches: &ArgMatches) -> Result<()> {
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

    program.run();

    Ok(())
}

pub fn xvm_list(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;

    let vdb = xvmlib::get_versiondb();

    if vdb.has_target(target) {
        let versions = vdb.get_all_version(target).unwrap_or_default();

        for version in versions {
            println!("{}", version);
        }
    } else { // print all matched targets
        let matches = vdb.match_by(target);

        for (name, versions) in matches {
            println!("{}:", name.bold());
            for version in versions {
                println!("  {}", version.cyan());
            }
        }
    }

    println!();

    Ok(())
}

pub fn xvm_workspace(matches: &ArgMatches) -> Result<()> {
    let target = matches.get_one::<String>("target").context("Target is required")?;
    let active = matches.get_one::<bool>("active");
    let inherit = matches.get_one::<bool>("inherit");

    let mut need_save = false;

    let mut workspace = if target == "global" {
        xvmlib::get_global_workspace().clone()
    } else {
        if fs::metadata("workspace.xvm.yaml").is_ok() {
            helper::load_local_workspace()
        } else {
            need_save = true;
            Workspace::new("workspace.xvm.yaml", target)
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
    }

    Ok(())
}