#pragma once
#include <string>
#include <vector>

namespace perlin {
    // The accounts the title screen's account switcher lists: Microsoft accounts (each with its own
    // saved Xbox sign-in file) and offline names. Stored in <data dir>/accounts.json.
    struct AccountEntry {
        std::string id;
        bool microsoft = false;
        std::string name;     // gamertag, or the offline name
        std::string authFile; // Microsoft only: sign-in cache, relative to the data dir
    };

    class AccountList {
    public:
        // Loads accounts.json; the first time, adopts the existing setup (auth.json and the offline
        // name) so nothing is lost.
        void Load(const std::string& dataDir, bool onlineLogin, const std::string& playerName);
        void Save() const;

        const std::vector<AccountEntry>& Entries() const { return entries; }
        const AccountEntry* Active() const;
        const std::string& ActiveId() const { return activeId; }
        bool SetActive(const std::string& id);

        // Adds (or, for a Microsoft account with the same gamertag, updates) an account and makes it active.
        const AccountEntry& AddOffline(const std::string& name);
        const AccountEntry& AddMicrosoft(const std::string& gamertag, const std::string& authFile);
        void Rename(const std::string& id, const std::string& name);
        // Removes the account; a Microsoft account's saved sign-in is deleted too.
        void Remove(const std::string& id);

        // A fresh sign-in cache path for a new Microsoft account (relative to the data dir).
        std::string NewAuthFile() const;
        std::string AuthPath(const AccountEntry& entry) const { return dataDir + "/" + entry.authFile; }

    private:
        std::string NewId(const char* prefix) const;
        std::string dataDir;
        std::vector<AccountEntry> entries;
        std::string activeId;
    };
}
