#include "Browser.h"
#include <AppCore/Dialogs.h>

static bool use_gpu_renderer = true;
static bool force_repaints = false;
static bool show_statistics = true;

void ShowOptionDialog()
{
    // Show a message box to select whether or not to use the GPU renderer
    ultralight::ButtonResult result = ultralight::ShowMessageBox("MiniBrowser Options",
		"Use the GPU renderer?\n\n(Yes = GPU, No = CPU)", ultralight::DialogIcon::Question, ultralight::ButtonType::YesNo);

	use_gpu_renderer = result == ultralight::ButtonResult::Yes;

	// Show a message box to select whether or not to force repaints
	result = ultralight::ShowMessageBox("MiniBrowser Options",
		"Force repaints to benchmark performance?\n\n(Yes = Force, No = Don't force)", ultralight::DialogIcon::Question, ultralight::ButtonType::YesNo);

    force_repaints = result == ultralight::ButtonResult::Yes;
}

int main()
{
    ShowOptionDialog();

    Browser browser(use_gpu_renderer, force_repaints, show_statistics);
    browser.Run();

    return 0;
}