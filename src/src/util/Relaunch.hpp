#pragma once

namespace perlin {
    void RememberCommandLine(int argc, char** argv);
    // Starts a fresh copy of this process with the same command line. `requested` is for a restart the player asked
    // for (a setting that needs one): it is not counted against the limit that stops a crash loop.
    bool RelaunchSelf(bool requested = false);
}
