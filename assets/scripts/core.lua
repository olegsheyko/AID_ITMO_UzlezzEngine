Core = uzlezz.behaviour {
    fields = {health = 10, pulse_radius = 6.0, pulse_cooldown = 2.0}
}

function Core:on_create()
    self.cooldown = 0.0
    assert(self.entity:get_field("health") > 0, "Core health must be positive")
end

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
end
