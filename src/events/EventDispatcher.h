#pragma once

#include <functional>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

class EventDispatcher {
public:
    template <typename TEvent>
    void addListener(std::function<void(const TEvent&)> listener) {
        static_assert(!std::is_reference_v<TEvent>, "Event type must not be a reference");
        listeners_[std::type_index(typeid(TEvent))].push_back(
            std::make_unique<Callback<TEvent>>(std::move(listener)));
    }

    template <typename TEvent>
    void dispatch(const TEvent& event) const {
        auto it = listeners_.find(std::type_index(typeid(TEvent)));
        if (it == listeners_.end()) {
            return;
        }

        for (const auto& callback : it->second) {
            callback->invoke(&event);
        }
    }

    void clear() {
        listeners_.clear();
    }

private:
    struct ICallback {
        virtual ~ICallback() = default;
        virtual void invoke(const void* event) const = 0;
    };

    template <typename TEvent>
    struct Callback final : ICallback {
        explicit Callback(std::function<void(const TEvent&)> listener)
            : listener_(std::move(listener)) {
        }

        void invoke(const void* event) const override {
            listener_(*static_cast<const TEvent*>(event));
        }

        std::function<void(const TEvent&)> listener_;
    };

    std::unordered_map<std::type_index, std::vector<std::unique_ptr<ICallback>>> listeners_;
};
