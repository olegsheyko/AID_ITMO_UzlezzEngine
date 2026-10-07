---Волны: Space запускает волну, враги спавнятся из префаба по кругу вокруг ядра.
---@class Waves : uzlezz.Behaviour
---@field wave integer номер текущей волны
---@field remaining integer врагов волны ещё не заспавнено
---@field timer number секунды до следующего спавна
---@field serial integer порядковый номер спавна в волне
---@field wave_count integer размер текущей волны
Waves = uzlezz.behaviour {
    fields = {first_wave_count = 4, count_increment = 2,
              spawn_interval = 0.6, spawn_radius = 10.0,
              enemy_prefab = "assets/prefabs/enemy.json"}
}

function Waves:on_create()
    assert(self.entity:get_field("first_wave_count") >= 1 and self.entity:get_field("count_increment") >= 0,
           "Wave count must be positive and increment nonnegative")
    assert(self.entity:get_field("spawn_interval") > 0 and self.entity:get_field("spawn_radius") > 1.2,
           "Spawn interval must be positive and radius > 1.2")
    self.wave, self.remaining, self.timer, self.serial = 0, 0, 0.0, 0
    self.wave_count = 0
end

function Waves:on_update(dt)
    local core = self.world:find_by_tag("Core")
    assert(core:is_alive(), "Wave scene needs a Core")
    local health = core:get_field("health")
    if health <= 0 then
        self.world:set_status(string.format("DEFEAT. Wave %d. Stop / Play to restart.", self.wave))
        return
    end
    local enemies = #self.world:find_all_by_tag("Enemy")
    if self.remaining == 0 and enemies == 0 and self.world:input_pressed("StartWave") then
        self.wave = self.wave + 1
        self.remaining = self.entity:get_field("first_wave_count") + (self.wave - 1) * self.entity:get_field("count_increment")
        self.wave_count = self.remaining
        self.serial = 0
        self.timer = 0.0
    end
    self.timer = self.timer - dt
    if self.remaining > 0 and self.timer <= 0.0 then
        -- Evenly spaced approaches, clockwise around the arena, reset for each wave.
        local angle = -math.pi / 2 + self.serial * (2 * math.pi / self.wave_count)
        local center = core:get_position()
        local radius = self.entity:get_field("spawn_radius")
        self.world:spawn_prefab(self.entity:get_field("enemy_prefab"),
            uzlezz.Vec3(center.x + math.cos(angle) * radius, center.y + math.sin(angle) * radius, 0.0))
        self.serial = self.serial + 1
        self.remaining = self.remaining - 1
        self.timer = self.entity:get_field("spawn_interval")
        enemies = enemies + 1
    end
    local hint = enemies == 0 and self.remaining == 0 and "Space: next wave" or "F: defense pulse"
    self.world:set_status(string.format("Core HP: %d | Wave: %d | Alive: %d | Pending: %d | %s",
        health, self.wave, enemies, self.remaining, hint))
end
