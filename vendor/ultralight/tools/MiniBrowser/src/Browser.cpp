#include "Browser.h"
#include "UI.h"
#include <Ultralight/Renderer.h>
#include <Ultralight/platform/Config.h>
#include <Ultralight/platform/Platform.h>
#include <sstream>

Browser::Browser(bool use_gpu, bool force_repaint, bool show_stats)
{
    Settings settings;
    settings.force_cpu_renderer = !use_gpu;

    Config config;
    config.scroll_timer_delay = 1.0 / 90.0;
    config.force_repaint = force_repaint;

#ifdef UL_EMBED_FILES
    // Use our custom FileSystem
    file_system_ = std::make_unique<EmbeddedFileSystem>();
    Platform::instance().set_file_system(file_system_.get());
#endif

    app_ = App::Create(settings, config);

    window_ = Window::Create(app_->main_monitor(), 1024, 768, false,
        kWindowFlags_Resizable | kWindowFlags_Titled | kWindowFlags_Maximizable);
    if (show_stats)
        window_->EnableFrameStatistics();
    SetTitle("Welcome!");

    // Create the UI
    ui_.reset(new UI(this));
    window_->set_listener(ui_.get());
}

Browser::~Browser()
{
    window_->set_listener(nullptr);

    ui_.reset();

    window_ = nullptr;
    app_ = nullptr;
}

void Browser::Run()
{
    app_->Run();
}

void Browser::SetTitle(const String& title)
{
    std::ostringstream str;
    str << "Ultralight MiniBrowser - " << title.utf8().data();
    window_->SetTitle(str.str().c_str());
}