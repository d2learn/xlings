use clap::{Arg, ArgAction, Command, ArgMatches, value_parser};
use anyhow::Result;

use crate::handler;

#[derive(Debug)]
pub struct CommandState {
    pub yes: bool,
    //pub verbose: bool,
}

pub fn run(matches: &ArgMatches) -> Result<()> {

    let cmd_state = CommandState {
        yes: matches.get_flag("yes"),
        //verbose: matches.get_flag("verbose"),
    };

    //println!("Command state: {:?}", cmd_state);

    match matches.subcommand() {
        Some(("add", sub_matches)) => handler::xvm_add(sub_matches, &cmd_state)?,
        Some(("remove", sub_matches)) => handler::xvm_remove(sub_matches, &cmd_state)?,
        Some(("use", sub_matches)) => handler::xvm_use(sub_matches, &cmd_state)?,
        Some(("current", sub_matches)) => handler::xvm_current(sub_matches, &cmd_state)?,
        Some(("run", sub_matches)) => handler::xvm_run(sub_matches, &cmd_state)?,
        Some(("list", sub_matches)) => handler::xvm_list(sub_matches, &cmd_state)?,
        Some(("workspace", sub_matches)) => handler::xvm_workspace(sub_matches, &cmd_state)?,
        _ => println!("Unknown command. Use --help for usage information."),
    }

    Ok(())
}

////////////

pub fn parse_from_command_line() -> ArgMatches {
    build_command().get_matches()
}

pub fn parse_from_string(args: &[&str]) -> ArgMatches {
    build_command()
        .try_get_matches_from(args)
        .expect("Failed to parse arguments from string")
}

fn build_command() -> Command {
    Command::new("xvm")
    .version("0.0.2") // prev-0.0.2
    .author("d2learn <dev@d2learn.com>")
    .about("a simple and generic version management tool")
    .arg(
        Arg::new("yes")
            .long("yes")
            .action(ArgAction::SetTrue)
            .global(true)
            .help("Automatically confirm prompts without asking"),
    )
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
                Arg::new("alias")
                    .long("alias")
                    .action(ArgAction::Set)
                    .help("Specify a alias for the target"),
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
                    //.required(true)
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
                    .num_args(0..)
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
                Arg::new("active")
                    .long("active")
                    .value_name("BOOL")
                    .action(ArgAction::Set)
                    .value_parser(value_parser!(bool))
                    .help("Set the workspace as active"),
            )
            .arg(
                Arg::new("inherit")
                    .long("inherit")
                    .value_name("BOOL")
                    .action(ArgAction::Set)
                    .value_parser(value_parser!(bool))
                    .help("Inherit from global workspace"),
            ),
    )
}