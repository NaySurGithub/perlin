#include "core/Server.hpp"

#include "core/Json.hpp"
#include "core/Log.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include "protocol/Packets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace perlin::server {
    namespace {
        std::string Trim(std::string s) {
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
            s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
            return s;
        }

        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // a block name with or without the namespace
        std::string FullBlockName(const std::string& n) { return n.find(':') == std::string::npos ? "minecraft:" + n : n; }
    }

    Server::Server(ServerConfig cfg) : config(std::move(cfg)) {
        tickRateTarget = std::max<uint32_t>(1, config.tickRate);
        loopback = std::make_unique<LoopbackListener>();
        plugins = std::make_unique<PluginHost>(*this);
    }

    Server::~Server() { Stop(); }

    bool Server::Start(std::string& error) {
        if (started)
            return true;
        if (config.generator != "flat") {
            error = "unknown generator '" + config.generator + "' (only \"flat\" exists yet)";
            return false;
        }
        {
            BlockCatalog& catalog = BlockCatalog::Instance();
            std::string catalogError;
            const std::string states = BlockCatalog::FindFile(config.blockDataDir, "block_states.json");
            const std::string names = BlockCatalog::FindFile(config.blockDataDir, "block_destroy.tsv");
            if (states.empty() || !catalog.Load(states, names, catalogError)) {
                catalog.LoadBuiltin();
                LogWarn("block data not found" + (catalogError.empty() ? std::string() : " (" + catalogError + ")") +
                        ": using the built-in list of " + std::to_string(catalog.Items().size()) +
                        " blocks; set block_data_dir to a folder with block_states.json and block_destroy.tsv");
            } else {
                LogInfo("block data: " + std::to_string(catalog.StateCount()) + " states, " + std::to_string(catalog.Items().size()) + " items");
            }
            BuildItemPackets();
        }
        BackendChoice choice = CreateTerrainBackend(config);
        if (!choice.backend) {
            error = "no terrain backend: " + choice.note;
            return false;
        }
        LogInfo("terrain: " + std::string(choice.backend->Name()) + (choice.note.empty() ? "" : " (" + choice.note + ")"));
        chunks = std::make_unique<ChunkManager>(std::move(choice.backend));

        RegisterBuiltins();

        std::string listenError;
        if (!loopback->Start(events, listenError)) {
            error = "loopback: " + listenError;
            return false;
        }
        if (config.nethernet && !config.loopbackOnly) {
            nethernet = CreateNetherNetListener(config, [this] {
                std::ostringstream o;
                o << "{\"name\":\"" << json::Escape(config.name) << "\",\"protocol\":2225,\"version\":\"1.26.60\",\"level\":\""
                  << json::Escape(config.levelName) << "\",\"players\":" << InGamePlayers().size() << ",\"maxPlayers\":" << config.maxPlayers
                  << ",\"gameType\":" << config.gameMode << "}";
                return o.str();
            });
            if (!nethernet) {
                error = "this build has no NetherNet support (rebuild with PERLIN_SERVER_NETHERNET=ON), or set loopback_only";
                return false;
            }
            if (!nethernet->Start(events, error))
                return false;
            LogInfo("nethernet signalling on " + config.bindAddress + ":" + std::to_string(config.port));
        }
        {
            const bool persist = !config.loopbackOnly;
            opsList.Load(persist ? config.dataDir + "/ops.json" : std::string());
            whitelistList.Load(persist ? config.dataDir + "/whitelist.json" : std::string());
            // the old operators key in server.json still counts
            for (const std::string& o : config.operators)
                opsList.Add(o, std::string());
            if (persist) {
                // written out on first run so there is a file to edit
                opsList.Save();
                whitelistList.Save();
            }
        }
        plugins->LoadDirectory(config.pluginDir);
        started = true;
        LogInfo("server '" + config.name + "' ready");
        return true;
    }

    void Server::Stop() {
        if (!started)
            return;
        started = false;
        for (auto& [id, session] : sessions)
            session->Kick("Server closed");
        sessions.clear();
        if (nethernet)
            nethernet->Stop();
        loopback->Stop();
        plugins->UnloadAll();
        chunks.reset();
    }

    void Server::Run() {
        using clock = std::chrono::steady_clock;
        auto next = clock::now();
        while (!stopRequested) {
            Tick();
            next += TickDelay();
            const auto now = clock::now();
            if (next < now) {
                // fell behind: drop the backlog instead of spiralling
                next = now;
            } else {
                std::this_thread::sleep_until(next);
            }
        }
    }

    std::chrono::nanoseconds Server::TickDelay() const {
        if (sprintRemaining > 0 || (frozen && stepRemaining > 0))
            return std::chrono::nanoseconds(0);
        return std::chrono::nanoseconds(static_cast<int64_t>(1'000'000'000.0 / tickRateTarget));
    }

    void Server::Tick() {
        const auto began = std::chrono::steady_clock::now();
        chunks->Pump();
        Pump();
        // frozen: the game clock holds still but the network, chat and commands keep working; a step lets single ticks through
        const bool advance = !frozen || stepRemaining > 0 || sprintRemaining > 0;
        if (advance) {
            tick++;
            if (frozen && stepRemaining > 0)
                stepRemaining--;
            if (plugins->AnyoneListens(PS_EVENT_TICK)) {
                PayloadWriter w;
                w.U64(tick);
                plugins->Dispatch(PS_EVENT_TICK, w.bytes);
            }
        }
        // position broadcast: once per tick for players that moved is plenty at 20 Hz
        std::vector<Session*> players = InGamePlayers();
        for (Session* s : players)
            if (s->TakeMoved())
                for (Session* other : players)
                    if (other != s)
                        other->SendMove(*s, 0);
        for (auto& [id, session] : sessions)
            session->Tick(tick);
        ReapClosed();
        if (advance) {
            tickMillis.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
            if (tickMillis.size() > 100)
                tickMillis.pop_front();
            if (sprintRemaining > 0 && --sprintRemaining == 0) {
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - sprintStarted).count();
                char report[160];
                std::snprintf(report, sizeof report, "Sprint completed with %.1f ticks per second, or %.2f ms per tick",
                              seconds > 0 ? static_cast<double>(sprintTotal) / seconds : 0.0, sprintTotal > 0 ? seconds * 1000.0 / static_cast<double>(sprintTotal) : 0.0);
                Broadcast(report);
            }
        }
    }

    std::string Server::TickQuery() const {
        char line[200];
        std::string out = sprintRemaining > 0 ? "The game is sprinting" : frozen ? "The game is frozen" : "The game is running normally";
        std::snprintf(line, sizeof line, "\nTarget tick rate: %.1f per second.", tickRateTarget);
        out += line;
        if (!tickMillis.empty()) {
            std::vector<double> sorted(tickMillis.begin(), tickMillis.end());
            std::sort(sorted.begin(), sorted.end());
            double sum = 0;
            for (double v : sorted)
                sum += v;
            const auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(p * static_cast<double>(sorted.size())))]; };
            std::snprintf(line, sizeof line, "\nAverage time per tick: %.1fms (Target: %.1fms)", sum / static_cast<double>(sorted.size()), 1000.0 / tickRateTarget);
            out += line;
            std::snprintf(line, sizeof line, "\nPercentiles: P50: %.1fms P95: %.1fms P99: %.1fms, sample: %zu", pct(0.5), pct(0.95), pct(0.99), sorted.size());
            out += line;
        }
        return out;
    }

    void Server::RunTickCommand(Session* from, const std::string& args) {
        const auto reply = [&](const std::string& text) {
            if (from)
                from->SendRaw(text);
            else
                LogInfo(text);
        };
        std::istringstream in(args);
        std::string sub, arg;
        in >> sub >> arg;
        sub = Lower(sub);
        // a time is ticks, or a number with t (ticks), s (seconds) or d (days) after it
        const auto parseTime = [](std::string text, int64_t& ticks) {
            if (text.empty())
                return false;
            double unit = 1;
            const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
            if (suffix == 't' || suffix == 's' || suffix == 'd') {
                unit = suffix == 's' ? 20.0 : suffix == 'd' ? 24000.0 : 1.0;
                text.pop_back();
            }
            try {
                size_t used = 0;
                const double v = std::stod(text, &used);
                if (used != text.size() || v < 1 || v * unit > 10'000'000)
                    return false;
                ticks = static_cast<int64_t>(v * unit);
                return true;
            } catch (...) {
                return false;
            }
        };
        if (sub == "query") {
            reply(TickQuery());
        } else if (sub == "rate") {
            try {
                size_t used = 0;
                const double v = std::stod(arg, &used);
                if (arg.empty() || used != arg.size() || v < 1.0 || v > 10000.0)
                    throw std::invalid_argument("range");
                tickRateTarget = v;
                char text[96];
                std::snprintf(text, sizeof text, "Set the target tick rate to %.1f per second", v);
                reply(text);
            } catch (...) {
                reply("§cUsage: /tick rate <1.0 - 10000.0>");
            }
        } else if (sub == "freeze") {
            frozen = true;
            reply("The game is frozen");
        } else if (sub == "unfreeze") {
            frozen = false;
            stepRemaining = 0;
            reply("The game is running normally");
        } else if (sub == "step") {
            if (arg == "stop") {
                if (stepRemaining > 0) {
                    stepRemaining = 0;
                    reply("Stopped tick stepping");
                } else {
                    reply("§cNo tick stepping in progress");
                }
                return;
            }
            int64_t n = 1;
            if (!arg.empty() && !parseTime(arg, n)) {
                reply("§cUsage: /tick step [time]");
                return;
            }
            if (!frozen) {
                reply("§cCannot step while the game is running; use /tick freeze first");
                return;
            }
            stepRemaining = n;
            reply("Stepping " + std::to_string(n) + " tick(s)");
        } else if (sub == "sprint") {
            if (arg == "stop") {
                if (sprintRemaining > 0) {
                    sprintRemaining = 0;
                    reply("Interrupted the current tick sprint");
                } else {
                    reply("§cNo tick sprint in progress");
                }
                return;
            }
            int64_t n = 0;
            if (!parseTime(arg, n)) {
                reply("§cUsage: /tick sprint <time|stop>");
                return;
            }
            sprintRemaining = sprintTotal = n;
            sprintStarted = std::chrono::steady_clock::now();
            reply("Sprinting " + std::to_string(n) + " tick(s)");
        } else {
            reply("§cUsage: /tick <query|rate|freeze|unfreeze|step|sprint>");
        }
    }

    void Server::Pump() {
        std::vector<NetEvent> batch;
        events.Drain(batch);
        for (NetEvent& e : batch) {
            switch (e.type) {
            case NetEvent::Type::Open: {
                const uint64_t id = nextRuntimeId++;
                sessions[id] = std::make_unique<Session>(*this, e.connection, id);
                e.connection->id = id;
                break;
            }
            case NetEvent::Type::Message:
                if (const auto it = sessions.find(e.connection->id); it != sessions.end())
                    it->second->OnMessage(e.data);
                break;
            case NetEvent::Type::Close:
                if (const auto it = sessions.find(e.connection->id); it != sessions.end())
                    it->second->Close();
                break;
            }
        }
    }

    void Server::ReapClosed() {
        for (auto it = sessions.begin(); it != sessions.end();) {
            if (it->second->IsClosed())
                it = sessions.erase(it);
            else
                ++it;
        }
    }

    std::vector<Session*> Server::InGamePlayers() {
        std::vector<Session*> out;
        for (auto& [id, s] : sessions)
            if (s->InGame())
                out.push_back(s.get());
        return out;
    }

    Session* Server::FindSession(uint64_t id) {
        const auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : it->second.get();
    }

    Session* Server::FindByName(const std::string& name) {
        const std::string wanted = Lower(name);
        for (auto& [id, s] : sessions)
            if (!s->Name().empty() && Lower(s->Name()) == wanted)
                return s.get();
        return nullptr;
    }

    bool Server::IsOperatorName(const std::string& name) const {
        // embedded singleplayer: the one player owns the world
        return config.loopbackOnly || opsList.Contains(name, std::string());
    }

    bool Server::IsOperator(const std::string& name, const std::string& xuid) const {
        return config.loopbackOnly || opsList.Contains(name, xuid);
    }

    std::string Server::SetOperator(const std::string& name, bool op) {
        Session* online = FindByName(name);
        const std::string shown = online ? online->Name() : name;
        if (op) {
            const bool added = opsList.Add(shown, online ? online->Xuid() : std::string());
            if (online)
                online->SetOperator(true);
            return added ? "Opped " + shown : shown + " is already an operator";
        }
        const bool removed = opsList.Remove(shown) || (online && !online->Xuid().empty() && opsList.Remove(online->Xuid()));
        if (online)
            online->SetOperator(false);
        return removed ? "De-opped " + shown : shown + " is not an operator";
    }

    bool Server::Full() const {
        size_t n = 0;
        for (const auto& [id, s] : sessions)
            if (s->GetState() != Session::State::Closed && !s->Name().empty())
                n++;
        return n > config.maxPlayers;
    }

    double Server::SpawnY() const { return static_cast<double>(FlatSurfaceY(config)); }

    void Server::OnPlayerJoined(Session& joined) {
        LogInfo(joined.Name() + " joined (" + joined.Conn().Describe() + ")");
        for (Session* other : InGamePlayers()) {
            if (other == &joined)
                continue;
            other->ShowPlayer(joined);
            joined.ShowPlayer(*other);
        }
        PayloadWriter w;
        w.U64(joined.Id());
        w.Str(joined.Name());
        plugins->Dispatch(PS_EVENT_PLAYER_JOIN, w.bytes);
        Broadcast("§e" + joined.Name() + " joined the game");
    }

    void Server::OnPlayerLeft(Session& left) {
        LogInfo(left.Name() + " left");
        for (Session* other : InGamePlayers())
            if (other != &left)
                other->HidePlayer(left);
        PayloadWriter w;
        w.U64(left.Id());
        w.Str(left.Name());
        plugins->Dispatch(PS_EVENT_PLAYER_QUIT, w.bytes);
        for (Session* other : InGamePlayers())
            if (other != &left)
                other->SendRaw("§e" + left.Name() + " left the game");
    }

    void Server::HandleChat(Session& from, const std::string& message) {
        if (!message.empty() && message[0] == '/') {
            RunPlayerCommand(from, message);
            return;
        }
        PayloadWriter w;
        w.U64(from.Id());
        w.Str(from.Name());
        w.Str(message);
        if (plugins->Dispatch(PS_EVENT_CHAT, w.bytes) & 1u)
            return;
        LogInfo("<" + from.Name() + "> " + message);
        for (Session* s : InGamePlayers())
            s->SendText(1, from.Name(), message);
    }

    void Server::RunPlayerCommand(Session& from, const std::string& line) {
        std::string text = Trim(line);
        if (!text.empty() && text[0] == '/')
            text.erase(0, 1);
        const size_t space = text.find(' ');
        const std::string name = Lower(text.substr(0, space));
        const std::string args = space == std::string::npos ? "" : Trim(text.substr(space + 1));
        const auto it = commands.find(name);
        if (it == commands.end()) {
            from.SendRaw("§cUnknown command: /" + name + ". Try /help");
            return;
        }
        if (it->second.operatorOnly && !from.Operator()) {
            from.SendRaw("§cYou do not have permission to use /" + name);
            return;
        }
        if (!it->second.plugin.empty()) {
            plugins->DispatchCommand(it->second.plugin, from.Id(), name, args);
            return;
        }
        it->second.run(&from, args);
    }

    void Server::RunConsoleCommand(const std::string& line) {
        std::string text = Trim(line);
        if (!text.empty() && text[0] == '/')
            text.erase(0, 1);
        if (text.empty())
            return;
        const size_t space = text.find(' ');
        const std::string name = Lower(text.substr(0, space));
        const std::string args = space == std::string::npos ? "" : Trim(text.substr(space + 1));
        const auto it = commands.find(name);
        if (it == commands.end()) {
            LogWarn("unknown command: " + name);
            return;
        }
        if (!it->second.plugin.empty())
            plugins->DispatchCommand(it->second.plugin, 0, name, args);
        else
            it->second.run(nullptr, args);
    }

    void Server::RegisterBuiltins() {
        const auto reply = [](Session* to, const std::string& text) {
            if (to)
                to->SendRaw(text);
            else
                LogInfo(text);
        };
        const auto add = [this](const char* name, const char* description, bool op, std::function<void(Session*, const std::string&)> fn,
                                const char* usage = "") {
            CommandDef d;
            d.name = name;
            d.description = description;
            d.usage = usage;
            d.operatorOnly = op;
            d.run = std::move(fn);
            commands[name] = std::move(d);
        };

        add("help", "List the commands you can use", false, [this, reply](Session* from, const std::string&) {
            std::string out = "§aCommands:";
            for (const auto& [n, d] : commands)
                if (!d.operatorOnly || !from || from->Operator())
                    out += "\n§7/" + n + "§r - " + d.description;
            reply(from, out);
        });
        add("list", "Show who is online", false, [this, reply](Session* from, const std::string&) {
            std::string names;
            const auto players = InGamePlayers();
            for (Session* p : players)
                names += (names.empty() ? "" : ", ") + p->Name();
            reply(from, std::to_string(players.size()) + "/" + std::to_string(config.maxPlayers) + " online: " + names);
        });
        add("say", "Broadcast a message", true, [this](Session*, const std::string& args) {
            if (!args.empty())
                Broadcast("§d[Server] " + args);
        }, "message");
        add("tp", "Teleport yourself", true, [reply](Session* from, const std::string& args) {
            if (!from) {
                reply(from, "only players can teleport");
                return;
            }
            std::istringstream in(args);
            double x, y, z;
            if (!(in >> x >> y >> z)) {
                reply(from, "§cUsage: /tp <x> <y> <z>");
                return;
            }
            // y is feet height; the protocol position is the eyes
            from->Teleport(x, y + 1.62, z);
        }, "x y z");
        add("kick", "Kick a player", true, [this, reply](Session* from, const std::string& args) {
            std::istringstream in(args);
            std::string who;
            in >> who;
            std::string reason;
            std::getline(in, reason);
            reason = Trim(reason);
            Session* target = FindByName(who);
            if (!target) {
                reply(from, "§cNo such player: " + who);
                return;
            }
            target->Kick(reason.empty() ? "Kicked by an operator" : reason);
        }, "player [reason]");
        add("plugins", "List loaded plugins", false, [this, reply](Session* from, const std::string&) {
            std::string out = "Plugins:";
            for (const auto& p : plugins->List())
                out += "\n" + p.name + (p.version.empty() ? "" : " " + p.version) + (p.enabled ? "" : " (disabled: " + p.error + ")");
            reply(from, out);
        });
        // targets: a name, @s (the sender), @p (nearest = the sender, else the first player) or @a (everyone)
        const auto targets = [this](Session* from, const std::string& who) {
            std::vector<Session*> out;
            if (who == "@a") {
                out = InGamePlayers();
            } else if (who == "@s" || who == "@p" || who == "@r") {
                if (from)
                    out.push_back(from);
                else if (auto all = InGamePlayers(); !all.empty())
                    out.push_back(all.front());
            } else if (Session* s = FindByName(who); s && s->InGame()) {
                out.push_back(s);
            }
            return out;
        };
        add("op", "Make a player an operator", true, [this, reply](Session* from, const std::string& args) {
            if (args.empty()) {
                reply(from, "§cUsage: /op <player>");
                return;
            }
            reply(from, SetOperator(args, true));
        }, "player");
        add("deop", "Remove a player's operator status", true, [this, reply](Session* from, const std::string& args) {
            if (args.empty()) {
                reply(from, "§cUsage: /deop <player>");
                return;
            }
            reply(from, SetOperator(args, false));
        }, "player");
        add("gamemode", "Change a player's game mode", true, [this, reply, targets](Session* from, const std::string& args) {
            std::istringstream in(args);
            std::string mode, who;
            in >> mode >> who;
            const std::string m = Lower(mode);
            int value = -1;
            if (m == "survival" || m == "s" || m == "0") value = 0;
            else if (m == "creative" || m == "c" || m == "1") value = 1;
            else if (m == "adventure" || m == "a" || m == "2") value = 2;
            if (value < 0) {
                reply(from, "§cUsage: /gamemode <survival|creative|adventure> [player]");
                return;
            }
            if (who.empty())
                who = "@s";
            const auto list = targets(from, who);
            if (list.empty()) {
                reply(from, "§cNo such player: " + who);
                return;
            }
            static const char* const names[] = {"Survival", "Creative", "Adventure"};
            for (Session* s : list) {
                s->SetGameMode(value);
                if (s != from)
                    s->SendRaw(std::string("Your game mode was set to ") + names[value]);
                reply(from, "Set " + s->Name() + "'s game mode to " + names[value]);
            }
        }, "mode [player]");
        add("give", "Give a player an item", true, [this, reply, targets](Session* from, const std::string& args) {
            std::istringstream in(args);
            std::string who, item;
            int count = 1;
            in >> who >> item;
            if (!(in >> count))
                count = 1;
            // "/give <item> [amount]" from a player gives to themselves
            if (!item.empty() && from && !FindByName(who) && who[0] != '@' && BlockCatalog::Instance().ItemByName(FullBlockName(Lower(who)))) {
                try { count = std::stoi(item); } catch (...) { count = 1; }
                item = who;
                who = "@s";
            }
            if (who.empty() || item.empty()) {
                reply(from, "§cUsage: /give <player> <item> [amount]");
                return;
            }
            count = std::clamp(count, 1, 2304);
            const CatalogItem* found = BlockCatalog::Instance().ItemByName(FullBlockName(Lower(item)));
            if (!found) {
                reply(from, "§cUnknown item: " + item);
                return;
            }
            const auto list = targets(from, who);
            if (list.empty()) {
                reply(from, "§cNo such player: " + who);
                return;
            }
            for (Session* s : list) {
                const int left = s->Inventory().Give(found->networkId, count);
                s->SendInventory();
                const std::string shown = found->name.substr(found->name.find(':') + 1);
                reply(from, "Gave " + std::to_string(count - left) + " " + shown + " to " + s->Name() + (left ? " (inventory full)" : ""));
            }
        }, "player item [amount]");
        add("whitelist", "Manage the whitelist", true, [this, reply](Session* from, const std::string& args) {
            std::istringstream in(args);
            std::string sub, who;
            in >> sub;
            std::getline(in, who);
            who = Trim(who);
            sub = Lower(sub);
            const auto persist = [this] {
                if (!config.loopbackOnly)
                    config.Save(config.dataDir + "/server.json");
            };
            if (sub == "on" || sub == "off") {
                config.whitelist = sub == "on";
                persist();
                reply(from, std::string("Whitelist is ") + (config.whitelist ? "on" : "off"));
            } else if (sub == "add" && !who.empty()) {
                Session* online = FindByName(who);
                const bool added = whitelistList.Add(online ? online->Name() : who, online ? online->Xuid() : std::string());
                reply(from, added ? "Added " + who + " to the whitelist" : who + " is already whitelisted");
            } else if (sub == "remove" && !who.empty()) {
                Session* online = FindByName(who);
                const bool removed = whitelistList.Remove(who) || (online && whitelistList.Remove(online->Xuid()));
                reply(from, removed ? "Removed " + who + " from the whitelist" : who + " is not whitelisted");
            } else if (sub == "list") {
                std::string out = std::string("Whitelist is ") + (config.whitelist ? "on" : "off") + ", " + std::to_string(whitelistList.Entries().size()) + " entries:";
                for (const AccessEntry& e : whitelistList.Entries())
                    out += "\n" + (e.name.empty() ? e.xuid : e.name);
                reply(from, out);
            } else if (sub == "reload") {
                whitelistList.Load(config.dataDir + "/whitelist.json");
                opsList.Load(config.dataDir + "/ops.json");
                reply(from, "Reloaded whitelist.json and ops.json");
            } else {
                reply(from, "§cUsage: /whitelist <on|off|add|remove|list|reload> [player]");
            }
        }, "action [player]");
        add("tick", "Query or control the game's tick rate", true, [this](Session* from, const std::string& args) { RunTickCommand(from, args); },
            "query|rate|freeze|unfreeze|step|sprint");
        add("stop", "Stop the server", true, [this](Session*, const std::string&) { RequestStop(); });
    }

    void Server::Broadcast(const std::string& text) {
        LogInfo(text);
        for (Session* s : InGamePlayers())
            s->SendRaw(text);
    }

    bool Server::Message(uint64_t player, const std::string& text) {
        // player 0 is the console
        if (player == 0) {
            LogInfo(text);
            return true;
        }
        Session* s = FindSession(player);
        if (!s || !s->InGame())
            return false;
        s->SendRaw(text);
        return true;
    }

    bool Server::Kick(uint64_t player, const std::string& reason) {
        Session* s = FindSession(player);
        if (!s)
            return false;
        s->Kick(reason);
        return true;
    }

    void Server::RegisterCommand(const std::string& plugin, const std::string& name, const std::string& description) {
        CommandDef d;
        d.name = Lower(name);
        d.description = description;
        d.plugin = plugin;
        commands[d.name] = std::move(d);
    }

    std::vector<PluginApi::PlayerInfo> Server::Players() {
        std::vector<PlayerInfo> out;
        for (Session* s : InGamePlayers())
            out.push_back({s->Id(), s->Name(), s->X(), s->Y() - 1.62, s->Z()});
        return out;
    }

    bool Server::Teleport(uint64_t player, double x, double y, double z) {
        Session* s = FindSession(player);
        if (!s || !s->InGame())
            return false;
        s->Teleport(x, y + 1.62, z);
        return true;
    }

    void Server::ApplyBlock(int x, int y, int z, uint16_t block) {
        chunks->SetBlock(x, y, z, block);
        // the cached payload for this column rebuilds on its version bump; players who already have it get the single change now
        const int32_t hash = BlockRegistry::Instance().Hash(block);
        for (Session* s : InGamePlayers())
            if (s->HasSent({x >> 4, z >> 4}))
                s->SendBlockUpdate(x, y, z, hash);
        if (plugins->AnyoneListens(PS_EVENT_BLOCK_CHANGE)) {
            PayloadWriter w;
            w.I32(x);
            w.I32(y);
            w.I32(z);
            w.Str(BlockRegistry::Instance().Name(block));
            plugins->Dispatch(PS_EVENT_BLOCK_CHANGE, w.bytes);
        }
    }

    void Server::SendBlockTo(Session& to, int x, int y, int z) {
        if (!chunks->Peek({x >> 4, z >> 4}))
            return;
        to.SendBlockUpdate(x, y, z, BlockRegistry::Instance().Hash(chunks->GetBlock(x, y, z)));
    }

    int Server::SetBlock(int x, int y, int z, const std::string& block) {
        if (y < kMinY || y >= kMinY + kHeight)
            return PS_ERR_BAD_ARGUMENTS;
        // by name means the default state; a block with no item is not one the catalog can place
        const CatalogItem* item = BlockCatalog::Instance().ItemByName(FullBlockName(block));
        const bool air = FullBlockName(block) == "minecraft:air";
        if (!item && !air)
            return PS_ERR_BAD_ARGUMENTS;
        if (!chunks->Peek({x >> 4, z >> 4}))
            return PS_ERR_NOT_LOADED;
        ApplyBlock(x, y, z, air ? BlockRegistry::kAir : BlockCatalog::Instance().RegistryId(item->blockHash));
        return PS_OK;
    }

    namespace {
        // block face index to the neighbour it points at, the order the game numbers faces in
        constexpr int kFaceOffset[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    }

    bool Server::PlayerBreak(Session& by, int x, int y, int z) {
        if (y < kMinY || y >= kMinY + kHeight || !by.InReach(x, y, z) || !chunks->Peek({x >> 4, z >> 4})) {
            by.CorrectBlock(x, y, z);
            return false;
        }
        const uint16_t current = chunks->GetBlock(x, y, z);
        if (current == BlockRegistry::kAir) {
            // a second message for a break that already happened: confirm, change nothing
            by.CorrectBlock(x, y, z);
            return true;
        }
        const std::string name = BlockRegistry::Instance().Name(current);
        if (!by.Creative() && name == "minecraft:bedrock") {
            by.CorrectBlock(x, y, z);
            return false;
        }
        if (plugins->AnyoneListens(PS_EVENT_BLOCK_BREAK)) {
            PayloadWriter w;
            w.U64(by.Id());
            w.I32(x);
            w.I32(y);
            w.I32(z);
            w.Str(name);
            if (plugins->Dispatch(PS_EVENT_BLOCK_BREAK, w.bytes) & 1u) {
                by.CorrectBlock(x, y, z);
                return false;
            }
        }
        ApplyBlock(x, y, z, BlockRegistry::kAir);
        // no item entities yet: a survival player gets the block straight into the inventory
        if (!by.Creative())
            if (const CatalogItem* item = BlockCatalog::Instance().ItemByName(name)) {
                by.Inventory().Give(item->networkId, 1);
                by.SendInventory();
            }
        return true;
    }

    bool Server::PlayerPlace(Session& by, int x, int y, int z, int face) {
        const auto reject = [&](int tx, int ty, int tz) {
            by.CorrectBlock(x, y, z);
            by.CorrectBlock(tx, ty, tz);
            by.SendSlot(by.Inventory().selected);
            return false;
        };
        if (face < 0 || face > 5)
            return false;
        const int tx = x + kFaceOffset[face][0], ty = y + kFaceOffset[face][1], tz = z + kFaceOffset[face][2];
        const ItemStack& held = by.Inventory().Held();
        const CatalogItem* item = held.Empty() ? nullptr : BlockCatalog::Instance().ItemById(held.networkId);
        if (!item || ty < kMinY || ty >= kMinY + kHeight || !by.InReach(tx, ty, tz) || !chunks->Peek({tx >> 4, tz >> 4}) ||
            chunks->GetBlock(tx, ty, tz) != BlockRegistry::kAir)
            return reject(tx, ty, tz);
        if (plugins->AnyoneListens(PS_EVENT_BLOCK_PLACE)) {
            PayloadWriter w;
            w.U64(by.Id());
            w.I32(tx);
            w.I32(ty);
            w.I32(tz);
            w.Str(item->name);
            if (plugins->Dispatch(PS_EVENT_BLOCK_PLACE, w.bytes) & 1u)
                return reject(tx, ty, tz);
        }
        ApplyBlock(tx, ty, tz, BlockCatalog::Instance().RegistryId(item->blockHash));
        if (!by.Creative()) {
            by.Inventory().TakeHeld(1);
            by.SendSlot(by.Inventory().selected);
        }
        return true;
    }

    int Server::GiveItem(uint64_t player, const std::string& itemName, int count) {
        Session* s = FindSession(player);
        if (!s || !s->InGame())
            return PS_ERR_NO_SUCH_PLAYER;
        const CatalogItem* item = BlockCatalog::Instance().ItemByName(FullBlockName(itemName));
        if (!item)
            return PS_ERR_UNKNOWN_ITEM;
        const int left = s->Inventory().Give(item->networkId, count);
        s->SendInventory();
        return left;
    }

    namespace {
        struct GroupDef {
            proto2225::CreativeContentPacketGroupsEntryCategory category;
            const char* name;
            // block name fragments that put an item in this group; the first group to match wins
            std::vector<const char*> words;
        };

        const std::vector<GroupDef>& Groups() {
            using C = proto2225::CreativeContentPacketGroupsEntryCategory;
            static const std::vector<GroupDef> groups = {
                {C::Construction, "itemGroup.name.planks", {"_planks"}},
                {C::Construction, "itemGroup.name.stairs", {"_stairs"}},
                {C::Construction, "itemGroup.name.slab", {"_slab"}},
                {C::Construction, "itemGroup.name.walls", {"_wall"}},
                {C::Construction, "itemGroup.name.fence", {"_fence"}},
                {C::Construction, "itemGroup.name.fenceGate", {"_fence_gate"}},
                {C::Construction, "itemGroup.name.door", {"_door"}},
                {C::Construction, "itemGroup.name.trapdoor", {"_trapdoor"}},
                {C::Construction, "itemGroup.name.glass", {"glass"}},
                {C::Construction, "itemGroup.name.wool", {"_wool"}},
                {C::Construction, "itemGroup.name.concrete", {"concrete"}},
                {C::Construction, "itemGroup.name.glazedTerracotta", {"glazed_terracotta"}},
                {C::Construction, "itemGroup.name.stainedClay", {"terracotta"}},
                {C::Construction, "itemGroup.name.copper", {"copper"}},
                {C::Construction, "itemGroup.name.stoneBrick", {"brick", "stone", "cobble", "deepslate", "tuff", "blackstone", "basalt", "andesite", "diorite", "granite", "prismarine", "purpur", "quartz", "sandstone"}},
                {C::Nature, "itemGroup.name.log", {"_log", "_wood", "_stem", "_hyphae"}},
                {C::Nature, "itemGroup.name.leaves", {"_leaves", "azalea_leaves"}},
                {C::Nature, "itemGroup.name.sapling", {"sapling", "propagule"}},
                {C::Nature, "itemGroup.name.flower", {"flower", "tulip", "orchid", "allium", "azure", "daisy", "cornflower", "lily", "rose", "peony", "lilac", "dandelion", "poppy", "sunflower"}},
                {C::Nature, "itemGroup.name.ore", {"_ore", "raw_", "ancient_debris"}},
                {C::Nature, "itemGroup.name.mineral", {"coal_block", "iron_block", "gold_block", "diamond_block", "emerald_block", "lapis_block", "netherite_block", "redstone_block"}},
                {C::Nature, "itemGroup.name.sand", {"sand", "gravel"}},
                {C::Nature, "itemGroup.name.dirt", {"dirt", "grass", "mycelium", "podzol", "farmland", "mud", "clay", "moss", "soul_soil", "path"}},
                {C::Nature, "itemGroup.name.ice", {"ice", "snow"}},
                {C::Nature, "itemGroup.name.mushroom", {"mushroom"}},
                {C::Nature, "itemGroup.name.coral", {"coral"}},
                {C::Nature, "itemGroup.name.seed", {"vine", "kelp", "seagrass", "fern", "bush", "cactus", "bamboo", "sugar_cane", "pumpkin", "melon", "hay", "crop", "wheat", "carrots", "potatoes", "beetroot"}},
                {C::Nature, "itemGroup.name.netherWartBlock", {"nether", "warped", "crimson", "shroomlight", "glowstone", "soul_"}},
                {C::Items, "itemGroup.name.rail", {"rail"}},
                {C::Items, "itemGroup.name.chest", {"chest", "barrel", "shulker"}},
                {C::Items, "itemGroup.name.lamp", {"lamp", "lantern", "torch", "candle", "campfire", "froglight"}},
                {C::Items, "itemGroup.name.sign", {"_sign"}},
                {C::Items, "itemGroup.name.bed", {"_bed"}},
                {C::Items, "itemGroup.name.banner", {"banner", "carpet"}},
                {C::Items, "itemGroup.name.redstone", {"redstone", "piston", "dispenser", "dropper", "observer", "repeater", "comparator", "lever", "button", "pressure_plate", "hopper", "tnt", "target", "sculk"}},
            };
            return groups;
        }
    }

    void Server::BuildItemPackets() {
        const BlockCatalog& catalog = BlockCatalog::Instance();
        proto2225::ItemRegistryPacket registry;
        proto2225::CreativeContentPacket creative;
        const std::vector<GroupDef>& defs = Groups();
        // one extra catch-all group at the end for blocks no fragment matched
        std::vector<proto2225::CreativeContentPacketGroupsEntry> groups(defs.size() + 1);
        for (size_t i = 0; i < defs.size(); i++) {
            groups[i].category = defs[i].category;
            groups[i].name = defs[i].name;
        }
        groups.back().category = proto2225::CreativeContentPacketGroupsEntryCategory::Items;
        groups.back().name = "itemGroup.name.miscFood";

        struct Placed {
            proto2225::ItemLegacy legacy;
            size_t group;
        };
        std::vector<Placed> placed;
        for (const CatalogItem& item : catalog.Items()) {
            proto2225::ItemstatesEntry entry;
            entry.name = item.name;
            entry.runtimeId = item.networkId;
            entry.componentBased = false;
            entry.version = proto2225::ItemstatesEntryVersion::Legacy;
            registry.itemstates.push_back(std::move(entry));

            proto2225::ItemLegacy legacy;
            legacy.networkId = item.networkId;
            legacy.count = 1;
            legacy.blockRuntimeId = item.blockHash;
            // the wire always carries the extra-data frame; leaving the variant empty writes none and the client cannot decode the packet
            legacy.extra = proto2225::ItemExtraDataWithoutBlockingTick{};
            size_t group = defs.size();
            const std::string shortName = item.name.substr(item.name.find(':') + 1);
            for (size_t g = 0; g < defs.size() && group == defs.size(); g++)
                for (const char* w : defs[g].words)
                    if (shortName.find(w) != std::string::npos) {
                        group = g;
                        break;
                    }
            placed.push_back({legacy, group});
        }
        // groups with nothing in them are left out; the client lists a tab per group
        std::vector<int> remap(groups.size(), -1);
        for (const Placed& p : placed)
            if (remap[p.group] < 0) {
                remap[p.group] = static_cast<int>(creative.groups.size());
                groups[p.group].iconItem = p.legacy;
                creative.groups.push_back(groups[p.group]);
            }
        // creative order: by group, the way the vanilla menu lists them
        std::stable_sort(placed.begin(), placed.end(), [&](const Placed& a, const Placed& b) { return remap[a.group] < remap[b.group]; });
        for (const Placed& p : placed) {
            proto2225::CreativeContentPacketItemsEntry e;
            e.entryId = static_cast<uint32_t>(p.legacy.networkId);
            e.item = p.legacy;
            e.groupIndex = static_cast<uint32_t>(remap[p.group]);
            creative.items.push_back(std::move(e));
        }
        net::EncodeChecked(registry, itemRegistryBody);
        net::EncodeChecked(creative, creativeBody);
    }

    int Server::GetBlock(int x, int y, int z, std::string& block) {
        if (!chunks->Peek({x >> 4, z >> 4}))
            return PS_ERR_NOT_LOADED;
        block = BlockRegistry::Instance().Name(chunks->GetBlock(x, y, z));
        return PS_OK;
    }

    PluginApi::Status Server::GetStatus() {
        Status s;
        s.name = config.name;
        s.players = static_cast<uint32_t>(InGamePlayers().size());
        s.maxPlayers = config.maxPlayers;
        s.tick = tick;
        s.backend = chunks ? chunks->BackendName() : "";
        return s;
    }

    std::string Server::Describe() const {
        return config.name + " (" + std::to_string(sessions.size()) + " connections, " + std::to_string(chunks ? chunks->Loaded() : 0) + " chunks)";
    }
}
