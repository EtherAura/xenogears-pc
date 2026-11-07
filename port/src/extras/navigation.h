#ifndef PORT_EXTRAS_NAVIGATION_H
#define PORT_EXTRAS_NAVIGATION_H

#include <string>
#include <vector>

namespace extras {

struct ViewerCommand { std::string name, argument; };
struct ViewerTarget {
    std::string viewer;
    std::vector<ViewerCommand> commands;
};

/* Saved links and scripts from the separate visual viewers enter the same
 * library, selecting their old category before applying asset selectors. */
ViewerTarget visualTarget(const ViewerTarget& target);

/* A host keeps suspended viewers alive, with their own sandboxes and UI
 * state. Navigation owns command strings and dispatches only when the host
 * is outside a viewer callback. Each registered viewer has one instance,
 * so revisiting a suspended viewer uses Back rather than opening it twice. */
class NavigationHost {
public:
    virtual ~NavigationHost() = default;
    virtual bool exists(const std::string& id) = 0;
    virtual void open(const std::string& id) = 0;
    virtual void suspend(const std::string& id) = 0;
    virtual void resume(const std::string& id) = 0;
    virtual void close(const std::string& id) = 0;
    virtual bool command(const ViewerCommand& command) = 0;
    virtual void failed(const std::string& message) = 0;
};

class ViewerNavigation {
public:
    static constexpr unsigned MAX_DEPTH = 8;
    bool follow(const ViewerTarget& target);
    bool back();
    void dispatch(NavigationHost& host);
    void clear(NavigationHost& host);
    void root(const std::string& id); /* after the host opens a root */
    const std::string& current() const { return current_; }
    const std::string& source() const;
    unsigned depth() const { return (unsigned)parents_.size(); }
    bool pending() const { return pending_ != NONE || dispatching_; }
private:
    enum Action { NONE, FOLLOW, BACK } pending_ = NONE;
    bool dispatching_ = false;
    ViewerTarget target_;
    std::string current_;
    std::vector<std::string> parents_;
};

} // namespace extras
#endif
