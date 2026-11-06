#pragma once

#include <memory>

// Разметка кадра на стороне видеокарты: именованные группы команд и имена GPU-объектов.
// Группы видны в отладчиках графики (RenderDoc, Nsight, Xcode) в дереве событий кадра,
// а на Windows те же группы попадают в Tracy как GPU-зоны с временем на видеокарте.
// Используется только внутри OpenGLRenderAdapter; остальной движок зовёт методы адаптера.
class GpuMarkers {
public:
    GpuMarkers();
    ~GpuMarkers();
    GpuMarkers(const GpuMarkers&) = delete;
    GpuMarkers& operator=(const GpuMarkers&) = delete;

    // Найти поддержанные расширения и загрузить функции. Звать после загрузки glad, при текущем GL-контексте.
    void init();
    // Открыть и закрыть именованную группу команд. Группы вкладываются; лишний pop игнорируется.
    void push(const char* name);
    void pop();
    // Дать GPU-объекту имя. khrIdentifier: тип объекта для KHR_debug, extType: для EXT_debug_label.
    void label(unsigned int khrIdentifier, unsigned int extType, unsigned int id, const char* name);
    // Раз в кадр после present: забрать времена GPU-зон для Tracy.
    void collect();
    // Есть ли хоть один приёмник разметки. Если нет, вызывающему незачем собирать имена групп.
    bool active() const;
    // Какой способ разметки нашёлся: "KHR_debug", "EXT_debug_marker" или "none" (для лога).
    const char* backend() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
