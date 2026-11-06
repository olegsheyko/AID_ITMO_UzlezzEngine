// Интеграционный тест скриптовой подсистемы (ЛР 2) на настоящих префабах игры: ядро, враги, спавнер волн.
// Проверяет игровой цикл (волны, урон, импульс, ввод), отсутствие утечек за 30 минут игры и 100 циклов Play/Stop,
// дескрипторы сущностей, ошибки Lua, подмену кода и сохранение полей в префаб.
// Запускается через ctest из корня репозитория (читает assets/prefabs). Первый аргумент это папка для временных файлов.
#include "scripting/ScriptSystem.h"
#include "prefabs/PrefabManager.h"
#include "ecs/Components.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Проверка: при невыполнении бросает исключение. main его ловит, печатает message и возвращает 1 (тест упал).
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Записывает текстовую фикстуру (например, Lua-файл) и падает, если запись не удалась.
void write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path);out<<text;require(bool(out),"fixture write failed");}
// Копия игрового префаба с закреплёнными числами: баланс в assets/prefabs крутят на демо, ожидания теста от него не зависят.
std::string pinned(const std::filesystem::path& dir,const std::string& name,const nlohmann::json& fields){
    std::ifstream in("assets/prefabs/"+name);require(bool(in),"prefab not found");
    nlohmann::json doc;in>>doc;doc["components"]["script"]["fields"]=fields;
    const auto path=dir/name;write(path,doc.dump(2));return path.string();
}
// Сначала играем в Arena на настоящих префабах, потом проверяем отдельные случаи на временном файле behaviour.lua.
int main(int argc,char** argv){
    try {
        const auto dir=std::filesystem::absolute(argc>1?argv[1]:"script-fixtures");
        std::filesystem::create_directories(dir);
        // Префабы игры с закреплёнными числами: враг (скорость 1.5, урон 1), ядро (10 HP),
        // спавнер (в первой волне 4 врага, +2 на каждую следующую).
        const std::string enemyPrefab=pinned(dir,"enemy.json",{{"speed",1.5},{"damage",1},{"attack_radius",1.2},{"animation_speed",1.0},{"target_tag","Core"}});
        const std::string corePrefab=pinned(dir,"core.json",{{"health",10},{"pulse_radius",6.0},{"pulse_cooldown",2.0}});
        const std::string wavesPrefab=pinned(dir,"waves.json",{{"first_wave_count",4},{"count_increment",2},{"spawn_interval",0.6},{"spawn_radius",10.0},{"enemy_prefab",enemyPrefab}});
        // Мир и скриптовая система. Сервисы движка подставляем сами, как это делает редактор:
        // спавн префаба и опрос действий ввода. wave и pulse имитируют нажатие Space (StartWave) и F (DefensePulse).
        World world;ScriptSystem scripts(world);
        scripts.spawnPrefab=[&](const std::string& path){return PrefabManager::spawn(world,path,false);};
        bool wave=false,pulse=false;
        scripts.inputPressed=[&](const std::string& name){return name=="StartWave"?wave:name=="DefensePulse"?pulse:false;};
        // Сцена: ядро и спавнер волн.
        Entity core=PrefabManager::spawn(world,corePrefab,false);
        PrefabManager::spawn(world,wavesPrefab,false);
        require(scripts.start(),scripts.error().c_str());
        // Без фокуса окна игры (allowInput=false) нажатие игнорируется: волна не стартует,
        // в мире остаются только ядро и спавнер.
        wave=true;scripts.update(.1f,false);
        require(world.getEntityCount()==2,"input must be gated by viewport focus");
        // С фокусом нажатие срабатывает: волна стартует и появляется первый враг.
        scripts.update(.1f,true);wave=false;
        require(world.getEntityCount()==3,"first enemy spawn");
        // 20 секунд игры по 0.05 с: все четыре врага доходят до ядра, бьют по 1 урона и исчезают (из 10 HP остаётся 6).
        for(int i=0;i<400;++i)scripts.update(.05f,true);
        require(scripts.error().empty(),scripts.error().c_str());
        require(std::get<int>(world.getComponent<ScriptComponent>(core).fields.at("health"))==6,"four enemies must damage core");
        require(world.getEntityCount()==2,"arrived enemies must be removed");
        // 30 minutes of simulated game time, continuously starting waves.
        // Replenish HP only in the test harness so gameplay remains active.
        std::size_t warmHeap=0;
        for(int i=0;i<36000;++i){
            world.getComponent<ScriptComponent>(core).fields["health"]=1000;
            wave=true;scripts.update(.05f,true);
            require(scripts.error().empty(),scripts.error().c_str());
            require(scripts.instanceCount()<100,"unexpected instance growth");
            if(i==1200)warmHeap=scripts.stats().peakMemoryBytes; // первая минута: волны уже идут
        }
        // Куча Lua за 30 минут не растёт: пик после первой минуты и пик за всю сессию близки.
        require(scripts.stats().peakMemoryBytes<warmHeap+(std::size_t(4)<<20),"Lua heap grows over a long session");
        // Защитный импульс: враг в радиусе импульса (расстояние 3 при радиусе 6) должен быть уничтожен.
        wave=false;scripts.stop();world.clear();
        core=PrefabManager::spawn(world,corePrefab,false);
        require(scripts.start(),scripts.error().c_str());
        auto enemy=PrefabManager::spawn(world,enemyPrefab,false);
        world.getComponent<Transform>(enemy).position={3,0,0};
        pulse=true;scripts.update(.1f,true);pulse=false;
        require(!world.isAlive(enemy),"pulse destroys enemy in range");
        // 100 раз подряд Stop, reload, Start: при Stop экземпляры освобождаются, ошибок нет.
        for(int i=0;i<100;++i){scripts.stop();require(scripts.instanceCount()==0,"stop releases instances");require(scripts.reload(),scripts.error().c_str());require(scripts.start(),scripts.error().c_str());scripts.update(.01f,false);}
        scripts.stop();world.clear();

        // Дальше проверяем отдельные случаи на временном файле behaviour.lua (класс Test), который тест переписывает по ходу.
        const auto path=dir/"behaviour.lua";
        const std::string header="Test = uzlezz.behaviour {fields={value=1, flag=true, speed=1.0, name='test'}}\n";
        const std::string source=header+R"(
function Test:on_create()
    _G.saved_entity, _G.saved_world = self.entity, self.world
end
function Test:on_update(dt)
    self.entity:set_field('value', self.entity:get_field('value') + 1)
end
function Test:on_destroy()
    _G.destroy_count = (_G.destroy_count or 0) + 1
end
)";
        // Обычная жизнь экземпляра: on_create, затем on_update на каждом кадре (value вырастает с 1 до 2 за один кадр).
        write(path,source);
        Entity e=world.createEntity();world.addComponent<Transform>(e);
        world.addComponent<ScriptComponent>(e,ScriptComponent{path.string(),"Test",{}, {}});
        require(scripts.start(),scripts.error().c_str());scripts.update(.1f,true);
        require(std::get<int>(world.getComponent<ScriptComponent>(e).fields.at("value"))==2,"C++ virtual adapter update");
        // Устаревшие дескрипторы: сущность уничтожена, а её id занят заново, но поколение (generation) уже другое.
        // Старый дескриптор из Lua не должен оживать и работать с новой сущностью.
        scripts.stop();world.destroyEntity(e);world.createEntityWithId(e);world.addComponent<Transform>(e);
        world.addComponent<ScriptComponent>(e,ScriptComponent{path.string(),"Test",{}, {}});
        write(path,header+R"(
function Test:on_create()
    assert(not _G.saved_entity:is_alive(), 'generation must reject reused ID')
    assert(not pcall(function() _G.saved_entity:get_position() end), 'stale component access must fail')
    assert(not pcall(function() _G.saved_world:find_by_tag('Core') end), 'old World proxy must not reactivate')
    assert(_G.destroy_count >= 1, 'virtual on_destroy must execute')
    _G.before_clear = self.entity
end
)");
        require(scripts.start(),scripts.error().c_str());scripts.stop();world.clear();
        // world.clear() тоже обесценивает дескрипторы живых сущностей.
        e=world.createEntity();world.addComponent<Transform>(e);
        world.addComponent<ScriptComponent>(e,ScriptComponent{path.string(),"Test",{}, {}});
        write(path,header+R"(
function Test:on_create()
    assert(not _G.before_clear:is_alive(), 'clear invalidates a live handle')
    assert(not pcall(function() _G.before_clear:get_position() end), 'clear invalidates component access')
end
)");
        require(scripts.start(),scripts.error().c_str());scripts.stop();
        // Ошибки загрузки: синтаксическая ошибка проваливает reload, а в сообщении есть имя файла.
        write(path,"this is invalid lua !\n");
        require(!scripts.reload(),"syntax error must fail reload");
        require(scripts.error().find("behaviour.lua")!=std::string::npos,"diagnostic must include file");
        // Ошибка времени выполнения в on_update перехватывается, в сообщении есть файл и строка.
        write(path,source+"\nfunction Test:on_update(dt) error('expected test error') end\n");
        require(scripts.start(),scripts.error().c_str());scripts.update(.1f,true);
        require(scripts.error().find("expected test error")!=std::string::npos,"runtime exception must be caught");
        require(scripts.error().find("behaviour.lua:")!=std::string::npos,"traceback file and line");
        // Исправленный файл снова загружается: восстановление после ошибки.
        scripts.stop();write(path,source);require(scripts.reload(),"recovery after error");
        // Подмена реализации: после reload выполняется новый on_update (value станет 99).
        write(path,source+"\nfunction Test:on_update(dt) self.entity:set_field('value', 99) end\n");
        require(scripts.reload(),"reload changed implementation");
        require(scripts.start(),scripts.error().c_str());scripts.update(.1f,true);
        require(std::get<int>(world.getComponent<ScriptComponent>(e).fields.at("value"))==99,"new Lua implementation must execute");
        scripts.stop();
        // Проверки класса при reload: обработчик, который не функция, и класс без базы uzlezz.behaviour отклоняются.
        write(path,header+"\nTest.on_update = 42\n");
        require(!scripts.reload(),"invalid callback must fail validation");
        write(path,"Test = {fields={value=1}}\n");
        require(!scripts.reload(),"behaviour must extend C++ base");
        world.clear();

        // Сохранение полей в префаб: правим health в компоненте и пишем в файл (PrefabManager::saveFields),
        // новый спавн из этого префаба должен получить 27.
        const auto prefab=dir/"saved.json";
        std::filesystem::copy_file("assets/prefabs/core.json",prefab,std::filesystem::copy_options::overwrite_existing);
        e=PrefabManager::spawn(world,prefab.string(),false);
        auto& component=world.getComponent<ScriptComponent>(e);component.fields["health"]=27;
        std::string error;require(PrefabManager::saveFields(component,error),error.c_str());
        Entity copy=PrefabManager::spawn(world,prefab.string(),false);
        require(std::get<int>(world.getComponent<ScriptComponent>(copy).fields.at("health"))==27,"saved prefab override must survive respawn");
        std::cout<<"ScriptSystemTests passed: 30 simulated minutes, waves, damage, pulse, input gating, 100 reload/play cycles, handles, exceptions, prefab persistence\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
