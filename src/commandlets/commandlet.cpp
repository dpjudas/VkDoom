
#include "commandlet.h"
#include "lightmapcmd.h"
#include "version.h"

void CommandletGroup::AddGroup(std::unique_ptr<CommandletGroup> group)
{
	Groups.Push(std::move(group));
}

void CommandletGroup::AddCommand(std::unique_ptr<Commandlet> command)
{
	Commands.Push(std::move(command));
}

/////////////////////////////////////////////////////////////////////////////

RootCommandlet::RootCommandlet()
{
	AddGroup<LightmapCmdletGroup>();
}

void RootCommandlet::RunCommand()
{
	FArgs args = *Args;
	args.RemoveArg(0);

	if (args.NumArgs() > 0 && stricmp(args.GetArg(0), "help") == 0)
	{
		args.RemoveArg(0);
		PrintDetailHelp(this, args);
	}
	else
	{
		RunCommand(this, args, {});
	}
}

void RootCommandlet::RunCommand(CommandletGroup* group, FArgs args, const FString prefix)
{
	if (args.NumArgs() > 0 && args.GetArg(0)[0] != '-')
	{
		const char* arg = args.GetArg(0);
		for (auto& cmd : group->Commands)
		{
			if (stricmp(arg, cmd->GetLongFormName().GetChars()) == 0)
			{
				args.RemoveArg(0);
				cmd->OnCommand(args);
				return;
			}
		}
		for (auto& subgroup : group->Groups)
		{
			if (stricmp(arg, subgroup->GetLongFormName().GetChars()) == 0)
			{
				args.RemoveArg(0);
				RunCommand(subgroup.get(), args, prefix + arg + " ");
				return;
			}
		}
		Printf("Unknown command " TEXTCOLOR_ORANGE "%s\n", arg);
	}
	else
	{
		Printf("Syntax: " TOOLNAMELOWERCASE TEXTCOLOR_ORANGE " <command> " TEXTCOLOR_CYAN "[options]" TEXTCOLOR_NORMAL " - Run command\n");
		Printf("Syntax: " TOOLNAMELOWERCASE " help " TEXTCOLOR_CYAN "[options]" TEXTCOLOR_NORMAL " - Get help list of commands\n");
		Printf("Syntax: " TOOLNAMELOWERCASE " help " TEXTCOLOR_ORANGE "<command> " TEXTCOLOR_CYAN "[options]" TEXTCOLOR_NORMAL " - Get help about a specific command\n");
	}
}

void RootCommandlet::PrintDetailHelp(CommandletGroup* group, FArgs args)
{
	if (args.NumArgs() > 0 && args.GetArg(0)[0] != '-')
	{
		const char* arg = args.GetArg(0);
		for (auto& cmd : group->Commands)
		{
			if (stricmp(arg, cmd->GetLongFormName().GetChars()) == 0)
			{
				cmd->OnPrintHelp();
				return;
			}
		}
		for (auto& subgroup : group->Groups)
		{
			if (stricmp(arg, subgroup->GetLongFormName().GetChars()) == 0)
			{
				args.RemoveArg(0);
				PrintDetailHelp(subgroup.get(), args);
				return;
			}
		}
		Printf("Unknown command " TEXTCOLOR_ORANGE "%s\n", arg);
	}
	else
	{
		PrintCommandList(this, {});
	}
}

void RootCommandlet::PrintCommandList(CommandletGroup* group, const FString prefix)
{
	if (group != this)
	{
		FString description = group->GetShortDescription();
		Printf("%s:\n", description.GetChars());
	}

	for (auto& cmdlet : group->Commands)
	{
		FString longname = prefix + cmdlet->GetLongFormName();
		FString description = cmdlet->GetShortDescription();
		while (longname.Len() < 20)
			longname.AppendCharacter(' ');
		Printf(TEXTCOLOR_ORANGE "%s " TEXTCOLOR_NORMAL "%s\n", longname.GetChars(), description.GetChars());
	}

	for (auto& subgroup : group->Groups)
	{
		PrintCommandList(subgroup.get(), prefix + subgroup->GetLongFormName() + " ");
	}
}
