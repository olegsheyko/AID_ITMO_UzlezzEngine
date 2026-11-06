---Враг: идёт к цели по тегу, бьёт её при подходе и исчезает.
---@class Enemy : uzlezz.Behaviour
-- Поля: speed (скорость в единицах мира за секунду), damage (сколько HP снимает у цели),
-- attack_radius (на каком расстоянии враг бьёт), animation_speed (скорость анимации ходьбы),
-- target_tag (тег цели, по умолчанию Core). Значения переопределяются в enemy.json.
Enemy = uzlezz.behaviour {
    fields = {speed = 1.5, damage = 1, attack_radius = 1.2,
              animation_speed = 1.0, target_tag = "Core"}
}

-- При появлении врага: проверяем баланс и ставим скорость анимации из поля.
function Enemy:on_create()
    assert(self.entity:get_field("speed") > 0 and self.entity:get_field("damage") > 0,
           "Enemy speed and damage must be positive")
    self.entity:set_animation_speed(self.entity:get_field("animation_speed"))
end

-- Каждый кадр: найти цель по тегу, пойти к ней по прямой; когда расстояние стало не больше attack_radius,
-- снять у цели damage очков HP (через set_field) и убрать самого себя. Если цели нет или её HP закончились,
-- враг останавливает анимацию и стоит.
function Enemy:on_update(dt)
    local core = self.world:find_by_tag(self.entity:get_field("target_tag"))
    if not core:is_alive() or core:get_field("health") <= 0 then
        self.entity:set_animation_speed(0.0)
        return
    end
    local p, target = self.entity:get_position(), core:get_position()
    local dx, dy = target.x - p.x, target.y - p.y
    local distance = math.sqrt(dx * dx + dy * dy)
    if distance <= math.max(0.1, self.entity:get_field("attack_radius")) then
        core:set_field("health", math.max(0, core:get_field("health") - self.entity:get_field("damage")))
        self.world:destroy(self.entity)
        return
    end
    -- Шаг не длиннее оставшегося расстояния, чтобы не проскочить цель. Потом поворот лицом к цели.
    local step = math.min(distance, self.entity:get_field("speed") * dt)
    self.entity:set_position(uzlezz.Vec3(p.x + dx / distance * step, p.y + dy / distance * step, p.z))
    self.entity:set_yaw(math.atan(dx, -dy))

    --while true do end 
end

