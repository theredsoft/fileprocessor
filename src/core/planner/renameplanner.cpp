#include "core/planner/renameplanner.h"

#include <unordered_map>

#include "core/template/textutil.h"

namespace fp::plan {
namespace {

bool isReservedBase(const std::string& nameUpper) {
    static const char* exact[] = {"CON", "PRN", "AUX", "NUL"};
    for (const char* r : exact)
        if (nameUpper == r) return true;
    if (nameUpper.size() == 4 &&
        (nameUpper.compare(0, 3, "COM") == 0 || nameUpper.compare(0, 3, "LPT") == 0) &&
        nameUpper[3] >= '1' && nameUpper[3] <= '9')
        return true;
    return false;
}

bool willRename(const PlanItem& item) { return item.issue == Issue::None; }

}  // namespace

Issue validateWindowsName(const std::string& name, std::string& message) {
    if (name.empty()) {
        message = "new name is empty";
        return Issue::EmptyName;
    }
    static const std::string forbidden = "\\/:*?\"<>|";
    for (const char c : name) {
        if (static_cast<unsigned char>(c) < 0x20) {
            message = "name contains a control character";
            return Issue::InvalidChar;
        }
        if (forbidden.find(c) != std::string::npos) {
            message = std::string("name contains forbidden character '") + c + "'";
            return Issue::InvalidChar;
        }
    }
    if (name.back() == '.' || name.back() == ' ') {
        message = "Windows does not allow names ending with a dot or a space";
        return Issue::TrailingDotOrSpace;
    }
    const std::string base = text::toUpper(name.substr(0, name.find('.')));
    if (isReservedBase(base)) {
        message = "'" + base + "' is a reserved device name on Windows";
        return Issue::ReservedName;
    }
    if (text::utf8Decode(name).size() > 255) {
        message = "name is longer than 255 characters";
        return Issue::TooLong;
    }
    return Issue::None;
}

Plan buildPlan(const tmpl::Template& tmpl, const std::vector<SourceFile>& files) {
    Plan plan;
    plan.items.reserve(files.size());

    for (const SourceFile& f : files) {
        PlanItem item;
        item.oldName = f.name;
        try {
            item.newName = tmpl.apply(f.ctx);
            if (item.newName == f.name) {
                item.issue = Issue::Unchanged;
                item.message = "name does not change";
            } else {
                std::string message;
                const Issue v = validateWindowsName(item.newName, message);
                if (v != Issue::None) {
                    item.issue = v;
                    item.message = std::move(message);
                }
            }
        } catch (const tmpl::EvalError& e) {
            item.issue = Issue::EvalFailed;
            item.message = e.what();
        }
        plan.items.push_back(std::move(item));
    }

    // Clash detection over FINAL names: a renamed file may take a name another
    // file is renamed away from (A→B, B→C is fine; the executor orders it),
    // but not a name that stays occupied or is produced twice.
    std::unordered_map<std::string, std::vector<size_t>> finalNames;
    for (size_t i = 0; i < plan.items.size(); ++i) {
        const PlanItem& item = plan.items[i];
        const std::string& finalName = willRename(item) ? item.newName : item.oldName;
        finalNames[text::toLower(finalName)].push_back(i);
    }
    for (const auto& [key, indexes] : finalNames) {
        if (indexes.size() < 2) continue;
        size_t keepers = 0;
        for (const size_t i : indexes)
            if (!willRename(plan.items[i])) ++keepers;
        for (const size_t i : indexes) {
            PlanItem& item = plan.items[i];
            if (!willRename(item)) continue;  // the occupying file isn't to blame
            item.issue = Issue::NameClash;
            item.message = keepers > 0
                               ? "the name '" + item.newName +
                                     "' is already taken by a file that keeps its name"
                               : "several files would end up with the same name '" +
                                     item.newName + "'";
        }
    }

    for (const PlanItem& item : plan.items) {
        if (item.issue == Issue::None)
            ++plan.renameCount;
        else if (item.issue != Issue::Unchanged)
            ++plan.conflictCount;
    }
    return plan;
}

}  // namespace fp::plan
