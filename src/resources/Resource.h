#pragma once

#include <string>
#include <memory>

// Базовый класс для всех ресурсов
class IResource {
public:
    virtual ~IResource() = default;
    
    const std::string& getPath() const { return path_; }
    bool isLoaded() const { return loaded_; }

protected:
    std::string path_;
    bool loaded_ = false;
};

// Шаблонная обёртка для типизированных ресурсов
template<typename T>
class Resource : public IResource {
public:
    Resource() = default;
    explicit Resource(const std::string& path) {
        path_ = path;
    }

    T* getData() { return &data_; }
    const T* getData() const { return &data_; }

    void setLoaded(bool loaded) { loaded_ = loaded; }

private:
    T data_;
};
