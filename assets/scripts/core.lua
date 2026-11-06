---Ядро: HP и защитный импульс по F. Поля баланса — в fields и префабе core.json.
---@class Core : uzlezz.Behaviour
---@field cooldown number секунды до следующего импульса
Core = uzlezz.behaviour {
    -- Поля баланса, их видно в Inspector и можно переопределить в core.json.
    -- health: очки жизни ядра, враги снимают их при подходе; pulse_radius: радиус импульса;
    -- pulse_cooldown: пауза между импульсами в секундах.
    fields = {health = 10, pulse_radius = 6.0, pulse_cooldown = 2.0}
}

-- Один раз при старте Play. self.cooldown живёт только в этом запуске (в fields его нет), поэтому
-- сбрасывается здесь. assert останавливает запуск с понятной ошибкой, если баланс задан неверно.
function Core:on_create()
    self.cooldown = 0.0
    assert(self.entity:get_field("health") > 0, "Core health must be positive")
end

-- Каждый кадр Play. Если ядро живо, нажата F и cooldown закончился, импульс уничтожает всех врагов
-- в радиусе pulse_radius (расстояние считается на плоскости XY). world:destroy только помечает врага,
-- удаляет его движок после обхода всех скриптов, поэтому прямо в цикле ничего не ломается.
function Core:on_update(dt)
    self.cooldown = math.max(0.0, self.cooldown - dt)
    if self.entity:get_field("health") <= 0 then return end
    if self.world:input_pressed("DefensePulse") and self.cooldown == 0.0 then
        local center = self.entity:get_position()
        local radius = math.max(0.0, self.entity:get_field("pulse_radius"))
        for _, enemy in ipairs(self.world:find_all_by_tag("Enemy")) do
            local p = enemy:get_position()
            local dx, dy = p.x - center.x, p.y - center.y
            if dx * dx + dy * dy <= radius * radius then
                self.world:destroy(enemy)
            end
        end
        self.cooldown = math.max(0.0, self.entity:get_field("pulse_cooldown"))
    end
    -- Если убрать комментарий со строки ниже, получится бесконечный цикл. Так проверяется песочница:
    -- через 100 мс движок прервёт вызов с ошибкой, а редактор не зависнет.
    --while true do end
end
