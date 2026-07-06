// TF-FTJ-Grupo3.cpp
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <memory>
#include <string>
#include <cmath>
#include <cstdlib>

#include "engine/Scene.h"
#include "engine/GameObject.h"
#include "engine/Component.h"
#include "engine/Transform.h"
#include "engine/SpriteRenderer.h"
#include "engine/SpriteAnimator.h"
#include "engine/RigidBody2D.h"
#include "engine/BoxCollider.h"
#include "engine/Camera.h"
#include "engine/FollowCamera.h"
#include "engine/Spawner.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Puntero global al jugador para que los monstruos lo sigan
GameObject* g_player = nullptr;

// Renderizador de un rectángulo de color sólido sin textura.
class ColorRectRenderer : public Component {
public:
    float width = 16.0f;
    float height = 16.0f;
    Uint8 r = 255, g = 255, b = 255, a = 255;

    void render() override {
        SDL_Renderer* renderer = gameObject->scene->getRenderer();
        Transform* t = gameObject->transform;
        Camera* cam = gameObject->scene->getActiveCamera();

        float centerX, centerY;
        if (cam) {
            cam->worldToScreen(t->x, t->y, centerX, centerY);
        } else {
            centerX = t->x;
            centerY = t->y;
        }

        SDL_FRect rect;
        rect.w = width * t->scaleX;
        rect.h = height * t->scaleY;
        rect.x = centerX - rect.w * 0.5f;
        rect.y = centerY - rect.h * 0.5f;

        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderFillRect(renderer, &rect);
    }
};

// Componente que almacena los atributos del jugador
class PlayerStats : public Component {
public:
    int maxHealth = 100;
    int currentHealth = 100;
    float baseSpeed = 160.0f;
    float currentSpeed = 160.0f;
    float damage = 25.0f;
    float fireRate = 0.25f; // cooldown en segundos entre disparos
    int fireType = 0;       // 0 = normal, 1 = doble, 2 = triple, 3 = cruz
    float scale = 2.0f;     // escala del transform del jugador

    void awake() override {
        updateTransformScale();
    }

    void takeDamage(int amount) {
        currentHealth -= amount;
        if (currentHealth < 0) currentHealth = 0;
        SDL_Log("Jugador recibio %d de dano. Vida actual: %d/%d", amount, currentHealth, maxHealth);
    }

    void heal(int amount) {
        currentHealth += amount;
        if (currentHealth > maxHealth) currentHealth = maxHealth;
        SDL_Log("Jugador se curo %d. Vida actual: %d/%d", amount, currentHealth, maxHealth);
    }

    // Setters y Getters
    void setSpeed(float s) { baseSpeed = currentSpeed = s; }
    float getSpeed() const { return currentSpeed; }

    void setHealth(int h) { maxHealth = currentHealth = h; }
    int getHealth() const { return currentHealth; }
    int getMaxHealth() const { return maxHealth; }

    void setDamage(float d) { damage = d; }
    float getDamage() const { return damage; }

    void setFireRate(float fr) { fireRate = fr; }
    float getFireRate() const { return fireRate; }

    void setFireType(int ft) { fireType = ft; }
    int getFireType() const { return fireType; }

    void setScale(float sc) { 
        scale = sc; 
        updateTransformScale();
    }
    float getScale() const { return scale; }

private:
    void updateTransformScale() {
        if (gameObject && gameObject->transform) {
            gameObject->transform->scaleX = scale;
            gameObject->transform->scaleY = scale;
        }
    }
};

// Componente que almacena los atributos de los enemigos
class EnemyStats : public Component {
public:
    float maxHealth = 50.0f;
    float currentHealth = 50.0f;
    float speed = 70.0f;
    float scale = 3.0f; // escala del transform del monstruo

    void awake() override {
        updateTransformScale();
    }

    void takeDamage(float amount) {
        currentHealth -= amount;
        if (currentHealth <= 0.0f) {
            SDL_Log("Monstruo eliminado!");
            gameObject->scene->destroy(gameObject);
        } else {
            SDL_Log("Monstruo recibio %.1f de dano. Vida restante: %.1f/%.1f", amount, currentHealth, maxHealth);
        }
    }

    void setSpeed(float s) { speed = s; }
    float getSpeed() const { return speed; }

    void setHealth(float h) { maxHealth = currentHealth = h; }
    float getHealth() const { return currentHealth; }

    void setScale(float sc) {
        scale = sc;
        updateTransformScale();
    }
    float getScale() const { return scale; }

private:
    void updateTransformScale() {
        if (gameObject && gameObject->transform) {
            gameObject->transform->scaleX = scale;
            gameObject->transform->scaleY = scale;
        }
    }
};

// Comportamiento de la bala que se destruye si sale de los límites de la pantalla
// o si choca contra un monstruo haciéndole daño.
class Bullet : public Component {
public:
    float damage = 25.0f;

    void update(float) override {
        int w = 1280, h = 720;
        SDL_Renderer* renderer = gameObject->scene->getRenderer();
        if (renderer) {
            SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
        }

        float limitX = w / 2.0f;
        float limitY = h / 2.0f;

        Transform* t = gameObject->transform;
        if (t->x < -limitX || t->x > limitX || t->y < -limitY || t->y > limitY) {
            gameObject->scene->destroy(gameObject);
        }
    }

    void onCollision(GameObject* other) override {
        if (other->name == "Monster") {
            auto enemyStats = other->getComponent<EnemyStats>();
            if (enemyStats) {
                enemyStats->takeDamage(damage);
            }
            gameObject->scene->destroy(gameObject);
        }
    }
};

// Controlador del monstruo para seguir al jugador en tiempo real.
class MonsterController : public Component {
public:
    std::string lastDir = "down";

    void update(float) override {
        if (!g_player) return;

        auto rb   = gameObject->getComponent<RigidBody2D>();
        auto anim = gameObject->getComponent<SpriteAnimator>();
        auto stats = gameObject->getComponent<EnemyStats>();
        
        float speed = stats ? stats->getSpeed() : 70.0f;

        Transform* t = gameObject->transform;
        Transform* pt = g_player->transform;

        // Calcular dirección hacia el jugador
        float dx = pt->x - t->x;
        float dy = pt->y - t->y;
        float length = std::sqrt(dx * dx + dy * dy);

        float mx = 0.0f;
        float my = 0.0f;

        if (length > 5.0f) {
            mx = dx / length;
            my = dy / length;
        }

        if (rb) {
            rb->velocityX = mx * speed;
            rb->velocityY = my * speed;
        }

        bool moving = (mx != 0.0f || my != 0.0f);
        if (moving) {
            // Eje dominante para elegir dirección de animación
            if (std::fabs(mx) > std::fabs(my)) {
                lastDir = (mx < 0.0f) ? "left" : "right";
            } else {
                lastDir = (my < 0.0f) ? "up" : "down";
            }
        }

        if (anim) {
            anim->play((moving ? "walk_" : "idle_") + lastDir);
        }
    }
};

// Paredes de spawn
enum class Wall { Top, Bottom, Left, Right };

// Función de spawn de monstruos
void spawnMonster(Scene& scene, Wall wall) {
    GameObject* monster = scene.createGameObject("Monster");
    
    int w = 1280, h = 720;
    SDL_Renderer* renderer = scene.getRenderer();
    if (renderer) {
        SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    }
    float halfW = w / 2.0f;
    float halfH = h / 2.0f;
    float spawnOffset = 40.0f;

    float sx = 0.0f;
    float sy = 0.0f;

    switch (wall) {
        case Wall::Top:
            sx = ((float)(std::rand() % (int)(halfW * 2.0f)) - halfW);
            sy = -halfH - spawnOffset;
            break;
        case Wall::Bottom:
            sx = ((float)(std::rand() % (int)(halfW * 2.0f)) - halfW);
            sy = halfH + spawnOffset;
            break;
        case Wall::Left:
            sx = -halfW - spawnOffset;
            sy = ((float)(std::rand() % (int)(halfH * 2.0f)) - halfH);
            break;
        case Wall::Right:
            sx = halfW + spawnOffset;
            sy = ((float)(std::rand() % (int)(halfH * 2.0f)) - halfH);
            break;
    }

    monster->transform->x = sx;
    monster->transform->y = sy;

    // Agregar y configurar estadísticas del enemigo según tipo aleatorio
    auto enemyStats = monster->addComponent<EnemyStats>();

    int monsterType = std::rand() % 4;
    std::string path;
    switch (monsterType) {
        case 0: // Mushroom: Seta rápida, frágil y pequeña
            path = "assets/ninja_adventure/Actor/Monster/Mushroom/mushroom.png";
            enemyStats->setHealth(40.0f);
            enemyStats->setSpeed(85.0f);
            enemyStats->setScale(2.5f);
            break;
        case 1: // Slime: Limo lento, resistente y mediano
            path = "assets/ninja_adventure/Actor/Monster/Slime/Slime.png";
            enemyStats->setHealth(80.0f);
            enemyStats->setSpeed(50.0f);
            enemyStats->setScale(3.0f);
            break;
        case 2: // Cyclope: Cíclope muy lento, tanque y grande
            path = "assets/ninja_adventure/Actor/Monster/Cyclope/SpriteSheet.png";
            enemyStats->setHealth(150.0f);
            enemyStats->setSpeed(35.0f);
            enemyStats->setScale(4.5f);
            break;
        case 3: // Spirit: Espíritu equilibrado, velocidad moderada
            path = "assets/ninja_adventure/Actor/Monster/Spirit/SpriteSheet.png";
            enemyStats->setHealth(60.0f);
            enemyStats->setSpeed(70.0f);
            enemyStats->setScale(3.2f);
            break;
    }

    monster->addComponent<SpriteRenderer>();
    auto anim = monster->addComponent<SpriteAnimator>(16, 16, 4);
    anim->addRowAnimation("walk_down",  path, 16, 16, 0, 8.0f);
    anim->addRowAnimation("walk_up",    path, 16, 16, 1, 8.0f);
    anim->addRowAnimation("walk_left",  path, 16, 16, 2, 8.0f);
    anim->addRowAnimation("walk_right", path, 16, 16, 3, 8.0f);
    anim->addRowAnimation("idle_down",  path, 16, 16, 0, 8.0f);
    anim->addRowAnimation("idle_up",    path, 16, 16, 1, 8.0f);
    anim->addRowAnimation("idle_left",  path, 16, 16, 2, 8.0f);
    anim->addRowAnimation("idle_right", path, 16, 16, 3, 8.0f);
    anim->play("idle_down");

    auto rb = monster->addComponent<RigidBody2D>();
    rb->gravityScale = 0.0f;

    auto col = monster->addComponent<BoxCollider>();
    col->width = 16.0f * enemyStats->getScale() * 0.7f;
    col->height = 16.0f * enemyStats->getScale() * 0.9f;

    monster->addComponent<MonsterController>();
}

// Movimiento libre en 4 direcciones sin gravedad (vista cenital), estilo Zelda.
class TopDownController : public Component {
public:
    std::string lastDir = "down"; // última dirección mirada (arranca mirando abajo)
    float shootTimer = 0.0f;

    void update(float dt) override {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        auto rb   = gameObject->getComponent<RigidBody2D>();
        auto anim = gameObject->getComponent<SpriteAnimator>();
        auto stats = gameObject->getComponent<PlayerStats>();

        float speed = stats ? stats->getSpeed() : 160.0f;

        // Movimiento por WASD
        float mx = 0.0f, my = 0.0f;
        if (keys[SDL_SCANCODE_A]) mx -= 1.0f;
        if (keys[SDL_SCANCODE_D]) mx += 1.0f;
        if (keys[SDL_SCANCODE_W]) my -= 1.0f;
        if (keys[SDL_SCANCODE_S]) my += 1.0f;
        
        if (rb) { 
            rb->velocityX = mx * speed; 
            rb->velocityY = my * speed; 
        }

        bool moving = (mx != 0.0f || my != 0.0f);
        if (moving) {
            if (mx < 0)      lastDir = "left";
            else if (mx > 0) lastDir = "right";
            else if (my < 0) lastDir = "up";
            else             lastDir = "down";
        }

        if (anim) anim->play((moving ? "walk_" : "idle_") + lastDir);

        // Limitar la posición al borde de la ventana de renderizado
        int w = 1280, h = 720;
        SDL_Renderer* renderer = gameObject->scene->getRenderer();
        if (renderer) {
            SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
        }
        
        float halfW = w / 2.0f;
        float halfH = h / 2.0f;
        
        float playerScale = stats ? stats->getScale() : 2.0f;
        float limitX = halfW - (16.0f * playerScale);
        float limitY = halfH - (16.0f * playerScale);
        
        Transform* t = gameObject->transform;
        if (t->x < -limitX) t->x = -limitX;
        if (t->x >  limitX) t->x =  limitX;
        if (t->y < -limitY) t->y = -limitY;
        if (t->y >  limitY) t->y =  limitY;

        // Disparo con Flechas
        if (shootTimer > 0.0f) {
            shootTimer -= dt;
        }

        float shootCooldown = stats ? stats->getFireRate() : 0.25f;

        if (shootTimer <= 0.0f) {
            float bulletDirX = 0.0f;
            float bulletDirY = 0.0f;
            bool wantsToShoot = false;

            if (keys[SDL_SCANCODE_UP]) {
                bulletDirY = -1.0f;
                wantsToShoot = true;
            }
            else if (keys[SDL_SCANCODE_DOWN]) {
                bulletDirY = 1.0f;
                wantsToShoot = true;
            }
            else if (keys[SDL_SCANCODE_LEFT]) {
                bulletDirX = -1.0f;
                wantsToShoot = true;
            }
            else if (keys[SDL_SCANCODE_RIGHT]) {
                bulletDirX = 1.0f;
                wantsToShoot = true;
            }

            if (wantsToShoot) {
                shoot(bulletDirX, bulletDirY);
                shootTimer = shootCooldown;
            }
        }
    }

private:
    void shoot(float dirX, float dirY) {
        auto stats = gameObject->getComponent<PlayerStats>();
        float dmg = stats ? stats->getDamage() : 25.0f;
        int type = stats ? stats->getFireType() : 0;

        if (type == 0) {
            // Disparo normal simple
            createBullet(dirX, dirY, dmg, 0.0f, 0.0f);
        }
        else if (type == 1) {
            // Disparo doble paralelo
            float perpX = -dirY * 15.0f;
            float perpY = dirX * 15.0f;
            createBullet(dirX, dirY, dmg, -perpX, -perpY);
            createBullet(dirX, dirY, dmg, perpX, perpY);
        }
        else if (type == 2) {
            // Disparo triple en abanico
            createBullet(dirX, dirY, dmg, 0.0f, 0.0f);
            
            float angle = 15.0f * (float)(M_PI / 180.0);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);
            
            float rx1 = dirX * cosA - dirY * sinA;
            float ry1 = dirX * sinA + dirY * cosA;
            createBullet(rx1, ry1, dmg, 0.0f, 0.0f);
            
            float rx2 = dirX * cosA - dirY * (-sinA);
            float ry2 = dirX * (-sinA) + dirY * cosA;
            createBullet(rx2, ry2, dmg, 0.0f, 0.0f);
        }
        else if (type == 3) {
            // Disparo en cruz
            createBullet(0.0f, -1.0f, dmg, 0.0f, 0.0f);
            createBullet(0.0f, 1.0f, dmg, 0.0f, 0.0f);
            createBullet(-1.0f, 0.0f, dmg, 0.0f, 0.0f);
            createBullet(1.0f, 0.0f, dmg, 0.0f, 0.0f);
        }
    }

    void createBullet(float dirX, float dirY, float dmg, float offsetX, float offsetY) {
        Scene* scene = gameObject->scene;
        GameObject* bullet = scene->createGameObject("Bullet");
        
        bullet->transform->x = gameObject->transform->x + offsetX;
        bullet->transform->y = gameObject->transform->y + offsetY;
        
        auto renderer = bullet->addComponent<ColorRectRenderer>();
        renderer->width = 12.0f;
        renderer->height = 12.0f;
        renderer->r = 255;
        renderer->g = 80;
        renderer->b = 80;
        
        auto rb = bullet->addComponent<RigidBody2D>();
        rb->gravityScale = 0.0f;
        
        float bulletSpeed = 400.0f;
        rb->velocityX = dirX * bulletSpeed;
        rb->velocityY = dirY * bulletSpeed;

        auto col = bullet->addComponent<BoxCollider>();
        col->width = 12.0f;
        col->height = 12.0f;
        col->isTrigger = true;

        auto bComp = bullet->addComponent<Bullet>();
        bComp->damage = dmg;
    }
};

void buildPlayerScene(Scene& scene) {
    const int   FRAME     = 32; // tamaño de cada cuadro en el sheet (128 / 4 = 32)
    const int   COL_DOWN  = 0;
    const int   COL_UP    = 1;
    const int   COL_LEFT  = 2;
    const int   COL_RIGHT = 3;
    const float ANIM_FPS  = 9.0f;

    const std::string SHEET = "assets/Personaje/monitasprites.png";

    GameObject* player = scene.createGameObject("Player");
    player->transform->x = 0.0f;
    player->transform->y = 0.0f;
    
    // Inicializar PlayerStats
    auto stats = player->addComponent<PlayerStats>();
    stats->setHealth(100);
    stats->setSpeed(160.0f);
    stats->setDamage(25.0f);
    stats->setFireRate(0.25f);
    stats->setFireType(0); // 0 = disparo normal
    stats->setScale(2.0f); // tamaño del jugador

    player->addComponent<SpriteRenderer>(); // sin textura: la pone el SpriteAnimator

    auto anim = player->addComponent<SpriteAnimator>(FRAME, FRAME, 4);
    anim->addLineAnimation("walk_down",  SHEET, FRAME, FRAME, COL_DOWN,  StripAxis::Column, ANIM_FPS);
    anim->addLineAnimation("walk_up",    SHEET, FRAME, FRAME, COL_UP,    StripAxis::Column, ANIM_FPS);
    anim->addLineAnimation("walk_left",  SHEET, FRAME, FRAME, COL_LEFT,  StripAxis::Column, ANIM_FPS);
    anim->addLineAnimation("walk_right", SHEET, FRAME, FRAME, COL_RIGHT, StripAxis::Column, ANIM_FPS);
    anim->addLineAnimation("idle_down",  SHEET, FRAME, FRAME, COL_DOWN,  StripAxis::Column, 0.0f);
    anim->addLineAnimation("idle_up",    SHEET, FRAME, FRAME, COL_UP,    StripAxis::Column, 0.0f);
    anim->addLineAnimation("idle_left",  SHEET, FRAME, FRAME, COL_LEFT,  StripAxis::Column, 0.0f);
    anim->addLineAnimation("idle_right", SHEET, FRAME, FRAME, COL_RIGHT, StripAxis::Column, 0.0f);
    anim->play("idle_down");

    auto rb = player->addComponent<RigidBody2D>();
    rb->gravityScale = 0.0f; // sin gravedad

    auto col = player->addComponent<BoxCollider>();
    col->width = 28.0f; col->height = 40.0f; col->offsetY = 8.0f;

    player->addComponent<TopDownController>();
    g_player = player; // Guardar referencia global al jugador

    // --- Cámara fija en el centro de la escena (0,0)
    GameObject* cam = scene.createGameObject("MainCamera");
    cam->addComponent<Camera>();

    // --- Spawnear los 10 primeros monstruos desde la derecha y arriba ---
    for (int i = 0; i < 10; ++i) {
        Wall startWall = (std::rand() % 2 == 0) ? Wall::Top : Wall::Right;
        spawnMonster(scene, startWall);
    }

    // --- Generador periódico de monstruos (desde las 4 paredes) ---
    GameObject* spawner = scene.createGameObject("MonsterSpawner");
    auto sp = spawner->addComponent<Spawner>();
    sp->interval = 2.0f; // un nuevo monstruo cada 2 segundos
    sp->spawn = [](Scene& s) {
        Wall wall = (Wall)(std::rand() % 4);
        spawnMonster(s, wall);
    };
}

int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Error al inicializar SDL: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("TF-FTJ-Grupo3: Top-Down Survival (WASD Mover, Flechas Disparar)", 1280, 720, 0);
    if (!window) { SDL_Quit(); return 1; }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    auto scene = std::make_unique<Scene>(renderer);
    buildPlayerScene(*scene);

    bool running = true;
    Uint64 lastTime = SDL_GetTicks();

    while (running) {
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTime) / 1000.0f;
        lastTime = now;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
        }

        scene->update(dt);

        // Fondo verde oscuro
        SDL_SetRenderDrawColor(renderer, 34, 59, 39, 255);
        SDL_RenderClear(renderer);
        
        scene->render();
        
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
