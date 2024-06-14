
#include "playgamepage.h"
#include "launcherwindow.h"
#include "i_interface.h"
#include "gstrings.h"
#include "version.h"
#include <zwidget/widgets/textlabel/textlabel.h>
#include <zwidget/widgets/listview/listview.h>
#include <zwidget/widgets/lineedit/lineedit.h>

PlayGamePage::PlayGamePage(LauncherWindow* launcher, WadStuff* wads, int numwads, int defaultiwad) : Widget(nullptr), Launcher(launcher)
{
	WelcomeLabel = new TextLabel(this);
	SelectLabel = new TextLabel(this);
#if defined(EXTRAARGS)
	ParametersLabel = new TextLabel(this);
#endif
	GamesList = new ListView(this);
#if defined(EXTRAARGS)
	ParametersEdit = new LineEdit(this);
#endif

	for (int i = 0; i < numwads; i++)
	{
		const char* filepart = strrchr(wads[i].Path.GetChars(), '/');
		if (filepart == NULL)
			filepart = wads[i].Path.GetChars();
		else
			filepart++;

		FString work;
		if (*filepart) work.Format("%s (%s)", wads[i].Name.GetChars(), filepart);
		else work = wads[i].Name.GetChars();

		GamesList->AddItem(work.GetChars());
	}

	if (defaultiwad >= 0 && defaultiwad < numwads)
	{
		GamesList->SetSelectedItem(defaultiwad);
		GamesList->ScrollToItem(defaultiwad);
	}

	GamesList->OnActivated = [=]() { OnGamesListActivated(); };
}

#if defined(EXTRAARGS)
void PlayGamePage::SetExtraArgs(const std::string& args)
{
	ParametersEdit->SetText(args);
}

std::string PlayGamePage::GetExtraArgs()
{
	return ParametersEdit->GetText();
}
#endif

int PlayGamePage::GetSelectedGame()
{
	return GamesList->GetSelectedItem();
}

void PlayGamePage::UpdateLanguage()
{
	SelectLabel->SetText(GStrings.GetString("PICKER_SELECT"));
#if defined(EXTRAARGS)
	ParametersLabel->SetText(GStrings.GetString("PICKER_ADDPARM"));
#endif
	FString welcomeText = GStrings.GetString("PICKER_WELCOME");
	welcomeText.Substitute("%s", GAMENAME);
	WelcomeLabel->SetText(welcomeText.GetChars());
}

void PlayGamePage::OnGamesListActivated()
{
	Launcher->Start();
}

void PlayGamePage::OnSetFocus()
{
	GamesList->SetFocus();
}

void PlayGamePage::OnGeometryChanged()
{
	double y = 10.0;

	WelcomeLabel->SetFrameGeometry(0.0, y, GetWidth(), WelcomeLabel->GetPreferredHeight());
	y += WelcomeLabel->GetPreferredHeight();

	y += 10.0;

	SelectLabel->SetFrameGeometry(0.0, y, GetWidth(), SelectLabel->GetPreferredHeight());
	y += SelectLabel->GetPreferredHeight();

	double listViewTop = y;

	y = GetHeight() - 10.0;

#if defined(EXTRAARGS)
	double editHeight = 24.0;
	y -= editHeight;
	ParametersEdit->SetFrameGeometry(0.0, y, GetWidth(), editHeight);
	y -= 5.0;

	double labelHeight = ParametersLabel->GetPreferredHeight();
	y -= labelHeight;
	ParametersLabel->SetFrameGeometry(0.0, y, GetWidth(), labelHeight);
	y -= 10.0;
#endif

	double listViewBottom = y - 10.0;
	GamesList->SetFrameGeometry(0.0, listViewTop, GetWidth(), std::max(listViewBottom - listViewTop, 0.0));
}
