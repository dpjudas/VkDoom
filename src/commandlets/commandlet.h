
#pragma once

#include <memory>
#include <functional>
#include "templates.h"
#include "zstring.h"
#include "printf.h"
#include "m_argv.h"

class Commandlet
{
public:
	virtual ~Commandlet() = default;

	virtual void OnCommand(FArgs args) = 0;
	virtual void OnPrintHelp() = 0;

	void RunInGame(std::function<void()> action);

	const FString& GetLongFormName() const { return LongFormName; }
	const FString& GetShortDescription() const { return ShortDescription; }

protected:
	void SetLongFormName(FString name) { LongFormName = std::move(name); }
	void SetShortDescription(FString desc) { ShortDescription = std::move(desc); }

private:
	FString LongFormName;
	FString ShortDescription;
};

class CommandletGroup
{
public:
	void AddGroup(std::unique_ptr<CommandletGroup> group);
	void AddCommand(std::unique_ptr<Commandlet> command);

	template<typename T>
	void AddGroup() { AddGroup(std::make_unique<T>()); }

	template<typename T>
	void AddCommand() { AddCommand(std::make_unique<T>()); }

	const FString& GetLongFormName() const { return LongFormName; }
	const FString& GetShortDescription() const { return ShortDescription; }

protected:
	void SetLongFormName(FString name) { LongFormName = std::move(name); }
	void SetShortDescription(FString desc) { ShortDescription = std::move(desc); }

private:
	FString LongFormName;
	FString ShortDescription;

	TArray<std::unique_ptr<CommandletGroup>> Groups;
	TArray<std::unique_ptr<Commandlet>> Commands;

	friend class RootCommandlet;
};

class RootCommandlet : public CommandletGroup
{
public:
	RootCommandlet();

	void RunCommand();
	static void RunEngineCommand();

private:
	void RunCommand(CommandletGroup* group, FArgs args, const FString prefix);
	void PrintCommandList(CommandletGroup* group, const FString prefix);
	void PrintDetailHelp(CommandletGroup* group, FArgs args);
};
