#include "GUI.hpp"
#include "../helpers/Logger.hpp"
#include "../core/Core.hpp"

using namespace GUI;

void GUI::updateEnv() {
    if (GUI::backend)
        GUI::backend->destroy();

    GUI::backend.reset();

    SP<Hyprutils::CLI::CLoggerConnection>       conn = makeShared<Hyprutils::CLI::CLoggerConnection>(*g_logger);
    Hyprtoolkit::IBackend::SBackendCreationData data;
    data.pLogConnection = conn;

    GUI::backend = Hyprtoolkit::IBackend::createWithData(data);

    available = !!GUI::backend;

    g_core->updateAvailability(available);

    if (GUI::backend)
        GUI::backend->destroy();

    GUI::backend.reset();
}
