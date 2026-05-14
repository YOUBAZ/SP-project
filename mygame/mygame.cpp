#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <SFML/Network.hpp>
#include <cmath>
#include <vector>
#include <optional>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <utility>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <string>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <array>
#include <map>
#include <queue>

using namespace std;
using namespace sf;

// ====================================================================================
// ================================== CONFIGURATION ===================================
// ====================================================================================
// Change this to your server IP when playing online
const std::string SERVER_IP = "127.0.0.1";
const unsigned short SERVER_PORT = 53000;

// ====================================================================================
// ================================== FILE STREAM =====================================
// ====================================================================================

struct UserRecord
{
    int id = 0;
    string name;
    string email;
    int imageNumber = -1;
    vector<int> friendIds;
    int barColor = 0;
    bool signedIn = false;
    vector<string> extraFields;
    bool gender;
};

static string trimCopy(const string& text)
{
    const auto first = find_if_not(text.begin(), text.end(), [](unsigned char ch)
        { return isspace(ch) != 0; });
    const auto last = find_if_not(text.rbegin(), text.rend(), [](unsigned char ch)
        { return isspace(ch) != 0; })
        .base();
    if (first >= last)
        return {};
    return string(first, last);
}

static string escapeField(const string& value)
{
    string out;
    out.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\':
            out += "\\\\";
            break;
        case '|':
            out += "\\|";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

static vector<string> splitEscaped(const string& line, char delimiter = '|')
{
    vector<string> fields;
    string current;
    bool escape = false;
    for (char ch : line)
    {
        if (escape)
        {
            switch (ch)
            {
            case 'n':
                current += '\n';
                break;
            case 'r':
                current += '\r';
                break;
            case 't':
                current += '\t';
                break;
            case '|':
                current += '|';
                break;
            case '\\':
                current += '\\';
                break;
            default:
                current += ch;
                break;
            }
            escape = false;
        }
        else if (ch == '\\')
            escape = true;
        else if (ch == delimiter)
        {
            fields.push_back(current);
            current.clear();
        }
        else
            current += ch;
    }
    if (escape)
        current += '\\';
    fields.push_back(current);
    return fields;
}

static bool parseInt(const string& text, int& value)
{
    try
    {
        size_t consumed = 0;
        const int parsed = stoi(text, &consumed);
        if (consumed != text.size())
            return false;
        value = parsed;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool parseFriendIds(const string& text, vector<int>& friendIds)
{
    friendIds.clear();
    const string cleaned = trimCopy(text);
    if (cleaned.empty())
        return true;
    string current;
    for (size_t i = 0; i <= cleaned.size(); ++i)
    {
        const char ch = (i < cleaned.size()) ? cleaned[i] : ',';
        if (ch == ',')
        {
            const string token = trimCopy(current);
            if (token.empty())
                return false;
            int friendId = 0;
            if (!parseInt(token, friendId))
                return false;
            friendIds.push_back(friendId);
            current.clear();
        }
        else
            current += ch;
    }
    return true;
}

static string joinFriendIds(const vector<int>& friendIds)
{
    string out;
    for (size_t i = 0; i < friendIds.size(); ++i)
    {
        if (i > 0)
            out += ',';
        out += to_string(friendIds[i]);
    }
    return out;
}

const UserRecord* findUserById(const vector<UserRecord>& users, int id)
{
    for (const UserRecord& user : users)
        if (user.id == id)
            return &user;
    return nullptr;
}

// Load users database
bool loadUsers(const string& filePath, vector<UserRecord>& users, string* errorMessage = nullptr)
{
    users.clear();
    ifstream in(filePath);
    if (!in)
    {
        if (errorMessage)
            *errorMessage = "Unable to open file for reading: " + filePath;
        return false;
    }
    string line;
    while (getline(in, line))
    {
        const string trimmed = trimCopy(line);
        if (trimmed.empty() || trimmed.rfind('#', 0) == 0)
            continue;
        const vector<string> fields = splitEscaped(line);
        if (fields.size() < 7)
        {
            if (errorMessage)
                *errorMessage = "Invalid record: expected at least 7 fields.";
            return false;
        }
        UserRecord user;
        if (!parseInt(fields[0], user.id))
            return false;
        user.name = fields[1];
        user.email = fields[2];
        if (!parseInt(fields[3], user.imageNumber))
            return false;
        if (!parseFriendIds(fields[4], user.friendIds))
            return false;
        if (!parseInt(fields[5], user.barColor))
            return false;
        int signedInValue = 0;
        if (!parseInt(fields[6], signedInValue))
            return false;
        user.signedIn = (signedInValue != 0);
        for (size_t i = 7; i < fields.size(); ++i)
            user.extraFields.push_back(fields[i]);
        users.push_back(move(user));
    }
    for (const UserRecord& user : users)
    {
        for (int friendId : user.friendIds)
        {
            if (!findUserById(users, friendId))
                return false;
        }
    }
    return true;
}

// Save users database
bool saveUsers(const string& filePath, const vector<UserRecord>& users, string* errorMessage = nullptr)
{
    ofstream out(filePath, ios::trunc);
    if (!out)
    {
        if (errorMessage)
            *errorMessage = "Unable to open file for writing: " + filePath;
        return false;
    }
    out << "# id|name|email|imageNumber|friendIds|barColor|signedIn|extraFields...\n";
    for (const UserRecord& user : users)
    {
        out << user.id << '|' << escapeField(user.name) << '|' << escapeField(user.email) << '|' << user.imageNumber << '|'
            << escapeField(joinFriendIds(user.friendIds)) << '|' << user.barColor << '|' << (user.signedIn ? 1 : 0);
        for (const string& extra : user.extraFields)
            out << '|' << escapeField(extra);
        out << '\n';
    }
    return true;
}

int nextUserId(const vector<UserRecord>& users)
{
    int maxId = 0;
    for (const UserRecord& user : users)
        maxId = max(maxId, user.id);
    return maxId + 1;
}

static int findUserIndexByEmail(const vector<UserRecord>& users, const string& email)
{
    for (size_t i = 0; i < users.size(); ++i)
        if (!users[i].email.empty() && users[i].email == email)
            return static_cast<int>(i);
    return -1;
}

static std::filesystem::path getAssetDirectory()
{
    static const std::filesystem::path assetDir = []()
        {
            std::vector<std::filesystem::path> searchRoots = { std::filesystem::current_path() };

            std::error_code ec;
            const auto sourcePath = std::filesystem::weakly_canonical(std::filesystem::path(__FILE__), ec);
            if (!ec)
                searchRoots.push_back(sourcePath.parent_path());

            for (const auto& root : searchRoots)
            {
                auto current = root;
                while (!current.empty())
                {
                    const auto candidate = current.filename() == "mygame" ? current : current / "mygame";
                    if (std::filesystem::exists(candidate / "Player.png") && std::filesystem::exists(candidate / "Star_Crush.ttf"))
                        return std::filesystem::absolute(candidate);

                    const auto parent = current.parent_path();
                    if (parent == current)
                        break;
                    current = parent;
                }
            }

            return std::filesystem::absolute(std::filesystem::path("mygame"));
        }();

    return assetDir;
}

static std::filesystem::path getAssetPath(const std::filesystem::path& filePath)
{
    if (filePath.is_absolute())
        return filePath;
    return getAssetDirectory() / filePath;
}

bool loadTextureSafe(sf::Texture& texture, const std::string& fileName)
{
    const auto path = getAssetPath(fileName);
    if (texture.loadFromFile(path.string()))
    {
        texture.setRepeated(false);
        return true;
    }
    return false;
}

bool openFontSafe(sf::Font& font, const std::string& fileName)
{
    return font.openFromFile(getAssetPath(fileName).string());
}

bool loadSoundBufferSafe(sf::SoundBuffer& buffer, const std::string& fileName)
{
    return buffer.loadFromFile(getAssetPath(fileName).string());
}

bool openMusicSafe(sf::Music& music, const std::string& fileName)
{
    return music.openFromFile(getAssetPath(fileName).string());
}

constexpr float VIRTUAL_SCREEN_WIDTH = 1920.f;
constexpr float VIRTUAL_SCREEN_HEIGHT = 1080.f;

static void applyLetterboxViewport(sf::View& view, sf::Vector2u windowSize)
{
    if (windowSize.x == 0 || windowSize.y == 0)
        return;

    const sf::Vector2f viewSize = view.getSize();
    if (viewSize.x <= 0.f || viewSize.y <= 0.f)
        return;

    const float windowRatio = static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y);
    const float viewRatio = viewSize.x / viewSize.y;

    float sizeX = 1.f, sizeY = 1.f;
    float posX = 0.f, posY = 0.f;

    if (windowRatio > viewRatio)
    {
        sizeX = viewRatio / windowRatio;
        posX = (1.f - sizeX) * 0.5f;
    }
    else if (windowRatio < viewRatio)
    {
        sizeY = windowRatio / viewRatio;
        posY = (1.f - sizeY) * 0.5f;
    }

    view.setViewport(sf::FloatRect({ posX, posY }, { sizeX, sizeY }));
}

static sf::View makeVirtualScreenView(const sf::RenderWindow& window)
{
    sf::View view;
    view.setCenter({ VIRTUAL_SCREEN_WIDTH * 0.5f, VIRTUAL_SCREEN_HEIGHT * 0.5f });
    view.setSize({ VIRTUAL_SCREEN_WIDTH, VIRTUAL_SCREEN_HEIGHT });
    applyLetterboxViewport(view, window.getSize());
    return view;
}

static sf::Vector2f mapPixelToVirtualScreen(const sf::RenderWindow& window, sf::Vector2i pixel)
{
    return window.mapPixelToCoords(pixel, makeVirtualScreenView(window));
}

// ====================================================================================
// ============================== ZOMBIE MODE NAMESPACE ===============================
// ====================================================================================
namespace ZombieMode
{
    struct Enemy
    {
        std::optional<sf::Sprite> shape;
        int type = 0;
        float hp = 0;
        float maxHp = 0;
        float damage = 0;
        float speed = 0;
        float radius = 12.5f;
        sf::Clock damageCooldown;
    };
    struct playerStats
    {
        std::optional<sf::Sprite> shape;
        float playerSpeed = 5.5f;
        float playerMaxHp = 100.f;
        float playerHp = 100.f;
        int current_level = 1;
    };
    struct Textures
    {
        sf::Texture player, walker, crawler, brute, wall, brokenWall, floor, barrel, explodedBarrel, heal, powerup, spawn, bullet, bulletShotgun;
    };
    Textures textures;

    bool loadZombieTextures()
    {
        bool ok = true;
        ok &= loadTextureSafe(textures.player, "Player.png");
        ok &= loadTextureSafe(textures.walker, "walker.png");
        ok &= loadTextureSafe(textures.crawler, "crawler.png");
        ok &= loadTextureSafe(textures.brute, "brute.png");
        ok &= loadTextureSafe(textures.wall, "wall.png");
        ok &= loadTextureSafe(textures.brokenWall, "brokenwall.png");
        ok &= loadTextureSafe(textures.floor, "floor.png");
        ok &= loadTextureSafe(textures.barrel, "barrel.png");
        ok &= loadTextureSafe(textures.explodedBarrel, "exploded.png");
        ok &= loadTextureSafe(textures.heal, "heal.png");
        ok &= loadTextureSafe(textures.powerup, "powerup.png");
        ok &= loadTextureSafe(textures.spawn, "spawn.png");
        ok &= loadTextureSafe(textures.bullet, "bullet.png");
        ok &= loadTextureSafe(textures.bulletShotgun, "bullet_shotgun.png");
        return ok;
    }

    constexpr int MAP_HEIGHT = 52, MAP_WIDTH = 52;
    constexpr float TILE_SIZE = 60.f;
    constexpr int PATH_TILE = 0, WALL = 1, BROKEN_WALL = 2, EMPTY_ENTITY = 0, SPAWN_POINT = 1, HEAL = 2, POWERUP = 3, BARREL = 4, EXPLODE_BARREL = 5;
    constexpr int ROOM_COUNT = 100, SPAWN_COUNT = 5, SPAWN_DISTANCE = 10, HEAL_COUNT = 20, HEAL_DISTANCE = 5, POWERUP_COUNT = 0, POWERUP_DISTANCE = 5, BARREL_COUNT = 25, BARREL_DISTANCE = 5, BARREL_DAMAGE = 200, BARREL_RADIUS = 2;
    int gameMap[MAP_HEIGHT][MAP_WIDTH][2];
    std::pair<int, int> usedItems[BARREL_COUNT + SPAWN_COUNT + HEAL_COUNT + POWERUP_COUNT];
    int usedCount = 0;

    // Flow field array for smart zombie pathfinding
    int flowField[MAP_HEIGHT][MAP_WIDTH];

    // Generate heatmap from player position
    void updateFlowField(sf::Vector2f playerPos)
    {
        for (int i = 0; i < MAP_HEIGHT; i++)
        {
            for (int j = 0; j < MAP_WIDTH; j++)
            {
                flowField[i][j] = 9999; // Set all tiles to unreachable initially
            }
        }

        // Find player cell
        int startX = std::max(0, std::min(MAP_WIDTH - 1, static_cast<int>(playerPos.x / TILE_SIZE)));
        int startY = std::max(0, std::min(MAP_HEIGHT - 1, static_cast<int>(playerPos.y / TILE_SIZE)));

        std::queue<std::pair<int, int>> q;
        q.push({ startY, startX });
        flowField[startY][startX] = 0; // Player cell is 0 distance

        int dr[] = { -1, 1, 0, 0, -1, -1, 1, 1 };
        int dc[] = { 0, 0, -1, 1, -1, 1, -1, 1 };

        // Calculate distances for all walkable cells
        while (!q.empty())
        {
            auto [r, c] = q.front();
            q.pop();
            int currentDist = flowField[r][c];

            for (int i = 0; i < 8; i++)
            {
                int nr = r + dr[i];
                int nc = c + dc[i];
                if (nr >= 0 && nr < MAP_HEIGHT && nc >= 0 && nc < MAP_WIDTH)
                {
                    if (gameMap[nr][nc][0] != WALL && flowField[nr][nc] == 9999)
                    {
                        flowField[nr][nc] = currentDist + 1;
                        q.push({ nr, nc });
                    }
                }
            }
        }
    }

    void initMap()
    {
        for (int i = 0; i < MAP_HEIGHT; i++)
            for (int j = 0; j < MAP_WIDTH; j++)
            {
                gameMap[i][j][0] = WALL;
                gameMap[i][j][1] = EMPTY_ENTITY;
            }
    }

    void makePath(int r, int c)
    {
        int dr[] = { -4, 4, 0, 0 }, dc[] = { 0, 0, -4, 4 }, order[] = { 0, 1, 2, 3 };
        for (int i = 3; i > 0; i--)
        {
            int random = rand() % (i + 1);
            int temp = order[i];
            order[i] = order[random];
            order[random] = temp;
        }
        gameMap[r][c][0] = PATH_TILE;
        gameMap[r + 1][c][0] = PATH_TILE;
        gameMap[r][c + 1][0] = PATH_TILE;
        gameMap[r + 1][c + 1][0] = PATH_TILE;
        for (int i = 0; i < 4; i++)
        {
            int nextrow = r + dr[order[i]], nextcol = c + dc[order[i]];
            if (nextrow > 0 && nextrow < MAP_HEIGHT - 2 && nextcol > 0 && nextcol < MAP_WIDTH - 2 && gameMap[nextrow][nextcol][0] == WALL)
            {
                int mr = r + dr[order[i]] / 2, mc = c + dc[order[i]] / 2;
                gameMap[mr][mc][0] = PATH_TILE;
                gameMap[mr + 1][mc][0] = PATH_TILE;
                gameMap[mr][mc + 1][0] = PATH_TILE;
                gameMap[mr + 1][mc + 1][0] = PATH_TILE;
                makePath(nextrow, nextcol);
            }
        }
    }

    void makeRooms(int rooms)
    {
        for (int i = 0; i < rooms; i++)
        {
            int roomheight = 3 + (rand() % 4) * 2, roomwidth = 3 + (rand() % 4) * 2;
            int roomrow = 1 + rand() % (MAP_HEIGHT - roomheight - 1), roomcol = 1 + rand() % (MAP_WIDTH - roomwidth - 1);
            for (int r = roomrow; r < roomrow + roomheight; r++)
                for (int c = roomcol; c < roomcol + roomwidth; c++)
                    gameMap[r][c][0] = PATH_TILE;
        }
    }

    bool distanceCheck(int r, int c, int mindistance)
    {
        for (int i = 0; i < usedCount; i++)
        {
            int rowdist = r - usedItems[i].first, coldist = c - usedItems[i].second;
            if (rowdist * rowdist + coldist * coldist < mindistance * mindistance)
                return false;
        }
        return true;
    }

    bool validZone(int r, int c, int type)
    {
        int dr = r - MAP_HEIGHT / 2, dc = c - MAP_WIDTH / 2, dist2 = dr * dr + dc * dc;
        if (type == SPAWN_POINT)
            return (r < 12 || r > MAP_HEIGHT - 13 || c < 12 || c > MAP_WIDTH - 13);
        if (type == POWERUP)
            return dist2 < 25 * 25;
        if (type == HEAL)
            return true;
        if (type == BARREL)
        {
            if (gameMap[r - 1][c][0] == WALL || gameMap[r + 1][c][0] == WALL || gameMap[r][c - 1][0] == WALL || gameMap[r][c + 1][0] == WALL)
                return true;
        }
        return false;
    }

    void placeEntitys(int count, int type, int mindistance)
    {
        int placed = 0, tries = 0, maxTries = count * 100;
        while (placed < count && tries < maxTries)
        {
            tries++;
            int r = rand() % MAP_HEIGHT, c = rand() % MAP_WIDTH;
            if (gameMap[r][c][0] == PATH_TILE && validZone(r, c, type) && distanceCheck(r, c, mindistance))
            {
                gameMap[r][c][1] = type;
                usedItems[usedCount++] = { r, c };
                placed++;
            }
        }
    }

    void generateMapLevel()
    {
        srand(static_cast<unsigned int>(time(NULL)));
        usedCount = 0;
        initMap();
        makePath(1, 1);
        makeRooms(ROOM_COUNT);
        placeEntitys(SPAWN_COUNT, SPAWN_POINT, SPAWN_DISTANCE);
        placeEntitys(HEAL_COUNT, HEAL, HEAL_DISTANCE);
        placeEntitys(POWERUP_COUNT, POWERUP, POWERUP_DISTANCE);
        placeEntitys(BARREL_COUNT, BARREL, BARREL_DISTANCE);
    }

    void explodeBarrel(int r, int c, std::vector<Enemy>& enemies)
    {
        if (gameMap[r][c][1] != BARREL)
            return;
        gameMap[r][c][1] = EXPLODE_BARREL;
        sf::Vector2f barrelPos((c * TILE_SIZE) + (TILE_SIZE / 2.f), (r * TILE_SIZE) + (TILE_SIZE / 2.f));
        float explosionRadiusPixels = 2.5f * TILE_SIZE;
        for (Enemy& enemy : enemies)
        {
            float dx = enemy.shape->getPosition().x - barrelPos.x, dy = enemy.shape->getPosition().y - barrelPos.y, dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= explosionRadiusPixels)
                enemy.hp -= (float)BARREL_DAMAGE * (1.f - (dist / explosionRadiusPixels));
        }
        for (int dr = -BARREL_RADIUS; dr <= BARREL_RADIUS; dr++)
        {
            for (int dc = -BARREL_RADIUS; dc <= BARREL_RADIUS; dc++)
            {
                int nr = r + dr, nc = c + dc;
                if (nr >= 0 && nr < MAP_HEIGHT && nc >= 0 && nc < MAP_WIDTH)
                {
                    if (gameMap[nr][nc][0] == WALL && nr != 0 && nc != 0 && nr != 51 && nc != 51)
                        gameMap[nr][nc][0] = BROKEN_WALL;
                    if (gameMap[nr][nc][1] == BARREL)
                        explodeBarrel(nr, nc, enemies);
                }
            }
        }
    }

    // Weapons setup
    constexpr int PISTOL_TYPE = 0, UZI_TYPE = 1, SHOTGUN_TYPE = 2, KATANA_TYPE = 3;
    int GUN_SCALE = 1;
    struct Bullet
    {
        std::optional<sf::Sprite> shape;
        sf::Vector2f velocity;
    };
    struct Gun
    {
        int type;
        float fireRate;
        float bulletSpeed;
        float damage;
        float range;
        bool automatic;
        sf::Clock fireClock;
    };
    struct gunSet
    {
        Gun pistol, uzi, shotgun, katana;
        Gun* currentGun;
    };
    gunSet guns;

    void initGuns()
    {
        guns.pistol = { PISTOL_TYPE, 2, 25, 20, 0, false };
        guns.uzi = { UZI_TYPE, 5.0, 20, 10, 0, true };
        guns.shotgun = { SHOTGUN_TYPE, 1, 30, 50, 0, false };
        guns.katana = { KATANA_TYPE, 2, 0, 50, 80, false };
        guns.currentGun = &guns.pistol;
    }

    void setupBulletSprite(sf::Sprite& sprite, const sf::Texture& texture, const sf::Vector2f& position, sf::Angle rotation)
    {
        sf::Vector2u size = texture.getSize();
        sprite.setOrigin({ size.x / 2.f, size.y / 2.f });
        sprite.setPosition(position);
        sprite.setRotation(rotation);
        sprite.setScale({ 30.f / size.x, 30.f / size.y });
    }

    void shootRegular(const playerStats& player, std::vector<Bullet>& bullets, Gun& gun, float angleRad)
    {
        Bullet b;
        b.shape.emplace(textures.bullet);
        setupBulletSprite(*b.shape, textures.bullet, player.shape->getPosition(), player.shape->getRotation());
        b.velocity = { std::cos(angleRad) * gun.bulletSpeed, std::sin(angleRad) * gun.bulletSpeed };
        bullets.push_back(b);
    }

    void shootShotgun(const playerStats& player, std::vector<Bullet>& bullets, Gun& gun)
    {
        float spread[] = { -10.f, 0.f, 10.f };
        for (int i = 0; i < 3; i++)
        {
            float angle = player.shape->getRotation().asDegrees() + spread[i], rad = angle * 3.14159f / 180.f;
            Bullet pellet;
            pellet.shape.emplace(textures.bulletShotgun);
            setupBulletSprite(*pellet.shape, textures.bulletShotgun, player.shape->getPosition(), sf::degrees(angle));
            pellet.velocity = { std::cos(rad) * gun.bulletSpeed, std::sin(rad) * gun.bulletSpeed };
            bullets.push_back(pellet);
        }
    }

    void shootKatana(const playerStats& player, std::vector<Enemy>& enemies, Gun& gun, float angleRad)
    {
        sf::Vector2f playerPos = player.shape->getPosition(), forward(std::cos(angleRad), std::sin(angleRad));
        for (auto& enemy : enemies)
        {
            sf::Vector2f toEnemy = enemy.shape->getPosition() - playerPos;
            float dist = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.y * toEnemy.y);
            if (dist > gun.range)
                continue;
            sf::Vector2f dir = toEnemy / dist;
            if ((forward.x * dir.x + forward.y * dir.y) > 0.5f)
                enemy.hp -= gun.damage;
        }
    }

    void shooting(playerStats& player, std::vector<Bullet>& bullets, std::vector<Enemy>& enemies, Gun& gun)
    {
        if (gun.fireClock.getElapsedTime().asSeconds() < 1.f / gun.fireRate)
            return;
        float angleRad = player.shape->getRotation().asRadians();
        if (gun.type == SHOTGUN_TYPE)
            shootShotgun(player, bullets, gun);
        else if (gun.type == KATANA_TYPE)
            shootKatana(player, enemies, gun, angleRad);
        else
            shootRegular(player, bullets, gun, angleRad);
        gun.fireClock.restart();
    }

    constexpr int WALKER_TYPE = 0, CRAWLER_TYPE = 1, BRUTE_TYPE = 2;
    struct KillTracker
    {
        int walkersKilled = 0, crawlersKilled = 0, walker_killed = 0, crawler_killed = 0, brute_killed = 0;
    };
    struct EnemySpawner
    {
        sf::Clock clock;
        float interval = 7.0f;
    };
    std::vector<sf::Vector2f> spawnPoints;

    bool isCollidingWithWall(const sf::Vector2f& center, float radius)
    {
        int minCol = std::max(0, (int)((center.x - radius) / TILE_SIZE)), maxCol = std::min(MAP_WIDTH - 1, (int)((center.x + radius) / TILE_SIZE));
        int minRow = std::max(0, (int)((center.y - radius) / TILE_SIZE)), maxRow = std::min(MAP_HEIGHT - 1, (int)((center.y + radius) / TILE_SIZE));
        for (int i = minRow; i <= maxRow; i++)
        {
            for (int j = minCol; j <= maxCol; j++)
            {
                if (gameMap[i][j][0] != WALL)
                    continue;
                float tileLeft = j * TILE_SIZE, tileTop = i * TILE_SIZE, closestX = std::max(tileLeft, std::min(center.x, tileLeft + TILE_SIZE)), closestY = std::max(tileTop, std::min(center.y, tileTop + TILE_SIZE)), dx = center.x - closestX, dy = center.y - closestY;
                if (dx * dx + dy * dy < radius * radius)
                    return true;
            }
        }
        return false;
    }

    void initSpawnPointsList()
    {
        spawnPoints.clear();
        for (int i = 0; i < MAP_HEIGHT; i++)
            for (int j = 0; j < MAP_WIDTH; j++)
                if (gameMap[i][j][1] == SPAWN_POINT)
                    spawnPoints.push_back({ (float)j * TILE_SIZE + TILE_SIZE / 2.f, (float)i * TILE_SIZE + TILE_SIZE / 2.f });
    }
    sf::Vector2f getPlayerStartPos()
    {
        for (int i = 1; i < MAP_HEIGHT; i++)
            for (int j = 1; j < MAP_WIDTH; j++)
                if (gameMap[i][j][0] == PATH_TILE)
                    return { (float)j * TILE_SIZE + TILE_SIZE / 2.f, (float)i * TILE_SIZE + TILE_SIZE / 2.f };
        return { 1.f, 1.f };
    }

    void scaleEnemyStats(Enemy& enemy, int level, int baseHp, int baseDamage, float baseSpeed)
    {
        if (level > 20)
        {
            enemy.hp = baseHp * 2;
            enemy.maxHp = baseHp * 2;
            enemy.damage = baseDamage * 2;
            enemy.speed = baseSpeed * 1.4f;
        }
        else if (level > 10)
        {
            enemy.hp = baseHp * 1.5f;
            enemy.maxHp = baseHp * 1.5f;
            enemy.damage = baseDamage * 1.5f;
            enemy.speed = baseSpeed * 1.2f;
        }
        else
        {
            enemy.hp = baseHp;
            enemy.maxHp = baseHp;
            enemy.damage = baseDamage;
            enemy.speed = baseSpeed;
        }
    }

    void spawnEnemy(int type, std::vector<Enemy>& enemies, playerStats& player)
    {
        if (spawnPoints.empty())
            return;
        Enemy enemy;
        enemy.type = type;
        if (type == WALKER_TYPE)
        {
            enemy.shape.emplace(textures.walker);
            sf::Vector2u size = textures.walker.getSize();
            enemy.shape->setOrigin({ size.x / 2.f, size.y / 2.f });
            enemy.shape->setScale({ 25.f / size.x, 25.f / size.y });
            enemy.radius = 12.5f;
            scaleEnemyStats(enemy, player.current_level, 100, 5, 5.0f);
        }
        else if (type == CRAWLER_TYPE)
        {
            enemy.shape.emplace(textures.crawler);
            sf::Vector2u size = textures.crawler.getSize();
            enemy.shape->setOrigin({ size.x / 2.f, size.y / 2.f });
            enemy.shape->setScale({ 30.f / size.x, 30.f / size.y });
            enemy.radius = 15.f;
            scaleEnemyStats(enemy, player.current_level, 200, 10, 5.5f);
        }
        else if (type == BRUTE_TYPE)
        {
            enemy.shape.emplace(textures.brute);
            sf::Vector2u size = textures.brute.getSize();
            enemy.shape->setOrigin({ size.x / 2.f, size.y / 2.f });
            enemy.shape->setScale({ 50.f / size.x, 50.f / size.y });
            enemy.radius = 25.f;
            scaleEnemyStats(enemy, player.current_level, 400, 30, 4.5f);
        }
        enemy.shape->setPosition(spawnPoints[rand() % spawnPoints.size()]);
        enemies.push_back(enemy);
    }

    void initPlayer(playerStats& player)
    {
        player.shape.emplace(textures.player);
        sf::Vector2u size = textures.player.getSize();
        player.shape->setOrigin({ size.x / 2.f, size.y / 2.f });
        player.shape->setPosition(getPlayerStartPos());
        player.shape->setScale({ 40.f / size.x, 40.f / size.y });
    }

    void updatePlayerMovement(playerStats& player, const std::vector<Enemy>& enemies)
    {
        sf::Vector2f movement(0.f, 0.f);
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W))
            movement.y -= player.playerSpeed;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S))
            movement.y += player.playerSpeed;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A))
            movement.x -= player.playerSpeed;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D))
            movement.x += player.playerSpeed;
        if (movement.x != 0.f && movement.y != 0.f)
            movement *= 0.7071f;
        if (!isCollidingWithWall(player.shape->getPosition() + sf::Vector2f(movement.x, 0.f), 18.f))
            player.shape->move({ movement.x, 0.f });
        if (!isCollidingWithWall(player.shape->getPosition() + sf::Vector2f(0.f, movement.y), 18.f))
            player.shape->move({ 0.f, movement.y });
    }

    void updatePlayerRotation(const sf::RenderWindow& window, playerStats& player, const sf::View& camera)
    {
        sf::Vector2f mouseWorldPos = window.mapPixelToCoords(sf::Mouse::getPosition(window), camera);
        sf::Vector2f playerPos = player.shape->getPosition();
        float angleDeg = std::atan2(mouseWorldPos.y - playerPos.y, mouseWorldPos.x - playerPos.x) * 180.f / 3.14159f;
        player.shape->setRotation(sf::degrees(angleDeg));
    }

    void drawTile(sf::RenderWindow& window, int i, int j)
    {
        float x = (float)j * TILE_SIZE, y = (float)i * TILE_SIZE;
        int terrain = gameMap[i][j][0], entity = gameMap[i][j][1];
        sf::Sprite floorSprite(textures.floor);
        floorSprite.setPosition({ x, y });
        floorSprite.setScale({ TILE_SIZE / textures.floor.getSize().x, TILE_SIZE / textures.floor.getSize().y });
        window.draw(floorSprite);
        if (terrain == WALL || terrain == BROKEN_WALL)
        {
            sf::Sprite wallSprite(terrain == WALL ? textures.wall : textures.brokenWall);
            wallSprite.setPosition({ x, y });
            wallSprite.setScale({ TILE_SIZE / textures.wall.getSize().x, TILE_SIZE / textures.wall.getSize().y });
            window.draw(wallSprite);
        }
        sf::Texture* entityTex = nullptr;
        if (entity == BARREL)
            entityTex = &textures.barrel;
        else if (entity == EXPLODE_BARREL)
            entityTex = &textures.explodedBarrel;
        else if (entity == HEAL)
            entityTex = &textures.heal;
        else if (entity == POWERUP)
            entityTex = &textures.powerup;
        else if (entity == SPAWN_POINT)
            entityTex = &textures.spawn;

        if (entityTex)
        {
            sf::Sprite entitySprite(*entityTex);
            entitySprite.setPosition({ x, y });
            entitySprite.setScale({ TILE_SIZE / entityTex->getSize().x, TILE_SIZE / entityTex->getSize().y });
            window.draw(entitySprite);
        }
    }

    void drawMap(sf::RenderWindow& window)
    {
        for (int i = 0; i < MAP_HEIGHT; i++)
            for (int j = 0; j < MAP_WIDTH; j++)
                drawTile(window, i, j);
    }

    void drawHpBar(sf::RenderWindow& window, sf::Vector2f pos, float hp, float maxHp, float width, float height, float offsetY, sf::Color color)
    {
        sf::RectangleShape base({ width, height });
        base.setFillColor(sf::Color::Red);
        base.setPosition({ pos.x - width / 2.f, pos.y + offsetY });
        sf::RectangleShape bar({ width * (hp / maxHp), height });
        bar.setFillColor(color);
        bar.setPosition(base.getPosition());
        window.draw(base);
        window.draw(bar);
    }

    void drawEnemies(sf::RenderWindow& window, const std::vector<Enemy>& enemies)
    {
        for (const auto& e : enemies)
        {
            window.draw(*e.shape);
            drawHpBar(window, e.shape->getPosition(), e.hp, e.maxHp, 40.f, 5.f, -35.f, sf::Color::Green);
        }
    }
    void drawPlayer(sf::RenderWindow& window, const playerStats& player)
    {
        window.draw(*player.shape);
        drawHpBar(window, player.shape->getPosition(), player.playerHp, player.playerMaxHp, 50.f, 6.f, -40.f, sf::Color::Cyan);
    }
    void drawEntities(sf::RenderWindow& window, const std::vector<Bullet>& bullets, const std::vector<Enemy>& enemies, const playerStats& player)
    {
        for (const auto& b : bullets)
            window.draw(*b.shape);
        drawEnemies(window, enemies);
        drawPlayer(window, player);
    }

    bool isCircleOverlapping(sf::Vector2f posA, float rA, sf::Vector2f posB, float rB)
    {
        float dx = posA.x - posB.x, dy = posA.y - posB.y;
        return (dx * dx + dy * dy) < (rA + rB) * (rA + rB);
    }

    void separateEnemies(Enemy& enemy, std::vector<Enemy>& enemies)
    {
        float radiusA = enemy.radius - 2.f;
        for (auto& other : enemies)
        {
            if (&enemy == &other)
                continue;
            float radiusB = other.radius - 2.f;
            if (!isCircleOverlapping(enemy.shape->getPosition(), radiusA, other.shape->getPosition(), radiusB))
                continue;
            sf::Vector2f push = enemy.shape->getPosition() - other.shape->getPosition();
            float pushDist = std::sqrt(push.x * push.x + push.y * push.y);
            if (pushDist == 0)
                continue;
            push /= pushDist;
            sf::Vector2f newPos = enemy.shape->getPosition() + push * 2.f;
            if (!isCollidingWithWall(newPos, radiusA))
                enemy.shape->move(push * 2.f);
        }
    }

    // Smart pathfinding using flow field
    void updateEnemies(std::vector<Enemy>& enemies, playerStats& player)
    {
        updateFlowField(player.shape->getPosition());

        for (auto& enemy : enemies)
        {
            sf::Vector2f enemyPos = enemy.shape->getPosition();
            int c = std::max(0, std::min(MAP_WIDTH - 1, static_cast<int>(enemyPos.x / TILE_SIZE)));
            int r = std::max(0, std::min(MAP_HEIGHT - 1, static_cast<int>(enemyPos.y / TILE_SIZE)));

            sf::Vector2f dir(0.f, 0.f);
            int minVal = flowField[r][c];
            int dr[] = { -1, 1, 0, 0, -1, -1, 1, 1 };
            int dc[] = { 0, 0, -1, 1, -1, 1, -1, 1 };

            for (int i = 0; i < 8; i++)
            {
                int nr = r + dr[i], nc = c + dc[i];
                if (nr >= 0 && nr < MAP_HEIGHT && nc >= 0 && nc < MAP_WIDTH)
                {
                    if (flowField[nr][nc] < minVal)
                    {
                        minVal = flowField[nr][nc];
                        dir = sf::Vector2f(dc[i], dr[i]);
                    }
                }
            }

            if (dir.x == 0.f && dir.y == 0.f)
                dir = player.shape->getPosition() - enemyPos;

            float length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (length > 0)
                dir /= length;
            float radius = enemy.radius - 2.f;

            if (!isCollidingWithWall(enemy.shape->getPosition() + sf::Vector2f(dir.x * enemy.speed, 0.f), radius))
                enemy.shape->move({ dir.x * enemy.speed, 0.f });
            if (!isCollidingWithWall(enemy.shape->getPosition() + sf::Vector2f(0.f, dir.y * enemy.speed), radius))
                enemy.shape->move({ 0.f, dir.y * enemy.speed });

            separateEnemies(enemy, enemies);

            float enemyRadius = enemy.radius, playerRadius = 18.f, minDist = enemyRadius + playerRadius;
            sf::Vector2f diff = enemy.shape->getPosition() - player.shape->getPosition();
            float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
            if (dist < minDist)
            {
                float overlap = minDist - dist;
                sf::Vector2f pushDir = dist > 0 ? diff / dist : sf::Vector2f(1.f, 0.f);
                sf::Vector2f newPos = enemy.shape->getPosition() + pushDir * overlap;
                if (!isCollidingWithWall(newPos, enemyRadius - 2.f))
                    enemy.shape->setPosition(newPos);
                else
                    enemy.shape->setPosition(enemy.shape->getPosition() + sf::Vector2f(pushDir.x * overlap, 0.f));
            }
            if (isCircleOverlapping(enemy.shape->getPosition(), enemyRadius, player.shape->getPosition(), playerRadius) && enemy.damageCooldown.getElapsedTime().asSeconds() >= 0.5f)
            {
                player.playerHp -= enemy.damage;
                enemy.damageCooldown.restart();
            }
        }
    }

    void updateBullets(std::vector<Bullet>& bullets, std::vector<Enemy>& enemies)
    {
        for (int i = 0; i < (int)bullets.size();)
        {
            bullets[i].shape->move(bullets[i].velocity);
            bool bulletDestroyed = false;
            int bCol = static_cast<int>(bullets[i].shape->getPosition().x / TILE_SIZE), bRow = static_cast<int>(bullets[i].shape->getPosition().y / TILE_SIZE);
            if (bCol < 0 || bCol >= MAP_WIDTH || bRow < 0 || bRow >= MAP_HEIGHT || gameMap[bRow][bCol][0] == WALL)
                bulletDestroyed = true;
            else if (gameMap[bRow][bCol][1] == BARREL)
            {
                explodeBarrel(bRow, bCol, enemies);
                bulletDestroyed = true;
            }
            else
            {
                for (auto& enemy : enemies)
                {
                    if (bullets[i].shape->getGlobalBounds().findIntersection(enemy.shape->getGlobalBounds()).has_value())
                    {
                        enemy.hp -= guns.currentGun->damage;
                        bulletDestroyed = true;
                        break;
                    }
                }
            }
            if (bulletDestroyed)
                bullets.erase(bullets.begin() + i);
            else
                i++;
        }
    }

    int computeLevel(const KillTracker& kills, int& remaining, int& pointsNeeded)
    {
        int total = kills.walker_killed + kills.crawler_killed * 3 + kills.brute_killed * 10;
        int level = 1;
        pointsNeeded = 10;
        remaining = total;
        while (remaining >= pointsNeeded)
        {
            remaining -= pointsNeeded;
            level++;
            pointsNeeded += 5;
        }
        return level;
    }
    void drawProgressBar(sf::RenderWindow& window, float percentage)
    {
        float barWidth = 775.f, barStartX = (VIRTUAL_SCREEN_WIDTH - barWidth) / 2.f;
        sf::RectangleShape bg({ barWidth, 30.f });
        bg.setFillColor(sf::Color::Cyan);
        bg.setPosition({ barStartX, 20.f });
        sf::RectangleShape fill({ percentage * 750.f, 20.f });
        fill.setFillColor(sf::Color::Green);
        fill.setPosition({ barStartX + 12.5f, 25.f });
        window.draw(bg);
        window.draw(fill);
    }

    void runZombieGame(sf::RenderWindow& window, int& menucounter)
    {
        playerStats player;
        KillTracker kills;
        EnemySpawner spawner;
        sf::View camera;
        sf::Font font;
        sf::Text LEVELText(font), scoreText(font);
        generateMapLevel();
        initSpawnPointsList();
        initPlayer(player);
        initGuns();
        camera.setSize({ 1280.f, 720.f });
        openFontSafe(font, "Star_Crush.ttf");
        LEVELText.setCharacterSize(32);
        LEVELText.setFillColor(sf::Color::White);
        scoreText.setCharacterSize(32);
        scoreText.setFillColor(sf::Color::White);
        std::vector<Bullet> bullets;
        std::vector<Enemy> enemies;
        sf::Clock clock;
        bool gameRunning = true;

        while (window.isOpen() && gameRunning)
        {
            while (const std::optional<sf::Event> event = window.pollEvent())
            {
                if (event->is<sf::Event::Closed>())
                {
                    window.close();
                    return;
                }
                if (const auto* key = event->getIf<sf::Event::KeyPressed>())
                {
                    if (key->code == sf::Keyboard::Key::Escape)
                    {
                        gameRunning = false;
                        menucounter = 6;
                        break;
                    }
                    if (key->code == sf::Keyboard::Key::Num1)
                        guns.currentGun = &guns.pistol;
                    if (key->code == sf::Keyboard::Key::Num2)
                        guns.currentGun = &guns.uzi;
                    if (key->code == sf::Keyboard::Key::Num3)
                        guns.currentGun = &guns.shotgun;
                    if (key->code == sf::Keyboard::Key::Num4)
                        guns.currentGun = &guns.katana;
                }
                if (const auto* mouseBtn = event->getIf<sf::Event::MouseButtonPressed>())
                {
                    if (mouseBtn->button == sf::Mouse::Button::Left)
                    {
                        if (guns.currentGun && !guns.currentGun->automatic)
                            shooting(player, bullets, enemies, *guns.currentGun);
                    }
                }
            }

            if (!gameRunning)
                break;

            if (guns.currentGun && guns.currentGun->automatic && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left))
                shooting(player, bullets, enemies, *guns.currentGun);
            if (spawner.clock.getElapsedTime().asSeconds() >= spawner.interval)
            {
                for (int i = 0; i < 4; i++)
                    spawnEnemy(WALKER_TYPE, enemies, player);
                spawner.clock.restart();
            }
            if (kills.brute_killed > 0)
                spawner.interval = std::max(1.0f, spawner.interval - 0.5f * kills.brute_killed);

            camera.setCenter(player.shape->getPosition());
            applyLetterboxViewport(camera, window.getSize());
            updatePlayerMovement(player, enemies);
            updatePlayerRotation(window, player, camera);
            int pCol = static_cast<int>(player.shape->getPosition().x / TILE_SIZE), pRow = static_cast<int>(player.shape->getPosition().y / TILE_SIZE);
            if (pRow >= 0 && pRow < MAP_HEIGHT && pCol >= 0 && pCol < MAP_WIDTH && gameMap[pRow][pCol][1] == HEAL && player.playerHp < player.playerMaxHp)
            {
                player.playerHp = std::min(player.playerHp + 25.f, player.playerMaxHp);
                gameMap[pRow][pCol][1] = EMPTY_ENTITY;
            }

            updateEnemies(enemies, player);
            updateBullets(bullets, enemies);

            for (int j = 0; j < (int)enemies.size();)
            {
                if (enemies[j].hp > 0)
                {
                    j++;
                    continue;
                }
                int deadType = enemies[j].type;
                enemies.erase(enemies.begin() + j);
                if (deadType == WALKER_TYPE)
                {
                    kills.walker_killed++;
                    kills.walkersKilled++;
                    if (kills.walkersKilled >= 15)
                    {
                        kills.walkersKilled -= 15;
                        for (int k = 0; k < 4; k++)
                            spawnEnemy(CRAWLER_TYPE, enemies, player);
                    }
                }
                else if (deadType == CRAWLER_TYPE)
                {
                    kills.crawler_killed++;
                    kills.crawlersKilled++;
                    if (kills.crawlersKilled >= 12)
                    {
                        kills.crawlersKilled -= 12;
                        spawnEnemy(BRUTE_TYPE, enemies, player);
                    }
                }
                else if (deadType == BRUTE_TYPE)
                    kills.brute_killed++;
            }

            int remaining, pointsNeeded;
            int calc_level = computeLevel(kills, remaining, pointsNeeded);
            if (calc_level > player.current_level)
            {
                player.current_level = calc_level;
                player.playerMaxHp = std::min(100.f + (player.current_level - 1) * 20.f, 375.f);
                player.playerHp = player.playerMaxHp;
                player.playerSpeed = std::min(5.5f + (player.current_level - 1) * 0.3f, 11.f);
                GUN_SCALE += 1;
            }
            if (player.playerHp <= 0)
            {
                std::cout << "Game Over!\n";
                gameRunning = false;
                menucounter = 6;
            }

            window.clear(sf::Color(30, 30, 30));
            window.setView(camera);
            drawMap(window);
            drawEntities(window, bullets, enemies, player);
            window.setView(makeVirtualScreenView(window));
            LEVELText.setString("LV: " + std::to_string(player.current_level));
            LEVELText.setPosition({ (VIRTUAL_SCREEN_WIDTH - 775.f) / 2.f, 60.f });
            scoreText.setString("SCORE: " + std::to_string(kills.walker_killed + kills.crawler_killed * 3 + kills.brute_killed * 10));
            scoreText.setPosition({ (VIRTUAL_SCREEN_WIDTH - 775.f) / 2.f + 775.f - 120.f, 60.f });
            drawProgressBar(window, (float)remaining / pointsNeeded);
            window.draw(LEVELText);
            window.draw(scoreText);

            int totalSec = static_cast<int>(clock.getElapsedTime().asSeconds());
            std::stringstream ss;
            ss << std::setfill('0') << std::setw(2) << totalSec / 3600 << ":" << std::setfill('0') << std::setw(2) << (totalSec % 3600) / 60 << ":" << std::setfill('0') << std::setw(2) << totalSec % 60;
            sf::Text timeText(font);
            timeText.setCharacterSize(32);
            timeText.setPosition({ 875, 60 });
            timeText.setString(ss.str());
            window.draw(timeText);
            window.display();
        }
        window.setView(makeVirtualScreenView(window));
    }
} // end ZombieMode

// ====================================================================================
// ============================== BOSS MODE NAMESPACE =================================
// ====================================================================================
namespace BossMode
{
    inline sf::Vector2f rotateVector(sf::Vector2f v, float radians)
    {
        const float cs = std::cos(radians), sn = std::sin(radians);
        return { v.x * cs - v.y * sn, v.x * sn + v.y * cs };
    }
    sf::Vector2f aimAt(sf::Vector2f from, sf::Vector2f to)
    {
        sf::Vector2f direction = to - from;
        float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        return (length > 0.f) ? direction / length : sf::Vector2f{ 1.f, 0.f };
    }
    bool isWithinRadius(sf::Vector2f from, sf::Vector2f to, float radius)
    {
        sf::Vector2f delta = from - to;
        return std::sqrt(delta.x * delta.x + delta.y * delta.y) < radius;
    }

    const float BOSS_MAX_HEALTH = 3000.f, BOSS_SPEED_BASE = 100.f, BOSS_FIRE_INTERVAL = 1.5f, PHASE2_HP_THRESHOLD = 0.75f, PHASE3_HP_THRESHOLD = 0.50f;
    const int MAX_BOSS_BULLETS = 60, MAX_GROUND_TRAPS = 20, MAX_BULLETS = 100;
    const float PLAYER_SPEED = 200.f, PLAYER_MAX_HEALTH = 100.f, SPECIAL_RECHARGE = 8.f, MAIN_FIRE_RATE = 0.25f, PLAYER_SPRITE_ROTATION_OFFSET = 270.f, BOSS_SPRITE_ROTATION_OFFSET = 270.f;

    enum class CharacterType
    {
        CHEESE_MAN,
        BAZOOKA_MAN,
        DETECTIVE,
        WIZARD,
        ICE_MAN
    };
    namespace CharacterSheetRow
    {
        constexpr int CHEESE_MAN = 0, BAZOOKA_MAN = 1, DETECTIVE = 2, WIZARD = 3, ICE_MAN = 4;
    }
    namespace CharacterFrameSize
    {
        constexpr int W = 32, H = 41;
    }
    namespace BossSheetRow
    {
        constexpr int OVERLORD = 0, MIRROR = 1, EVOLUTION = 2;
    }
    namespace BossFrameSize
    {
        constexpr int W = 87, H = 136;
    }

    enum class EffectType
    {
        NONE,
        STUCK,
        SLOWED,
        CONFUSED,
        FROZEN,
        DISTORTED
    };
    enum class BulletOwner
    {
        PLAYER,
        ENEMY,
        BOSS
    };
    enum class BulletSpecial
    {
        NORMAL,
        CHEESE,
        BAZOOKA,
        REVOLVER,
        MAGIC,
        ICE,
        CHEESE_TRAP,
        MINI_BAZOOKA,
        CLUSTER,
        MULTI_PENETRATE,
        HEAT_BURST,
        TELEPORT_SHOT
    };

    struct SpriteAnimator
    {
        int frameCount = 4, frameW = 32, frameH = 41, sheetRow = 0, currentFrame = 0;
        float frameInterval = 0.12f, frameTimer = 0.f;
        bool isWalking = false;
        void setup(int row, int fw, int fh, int frames = 4, float fps = 8.f)
        {
            sheetRow = row;
            frameW = fw;
            frameH = fh;
            frameCount = frames;
            frameInterval = 1.f / fps;
            currentFrame = 0;
            frameTimer = 0.f;
        }
        void update(float dt, bool isMoving)
        {
            isWalking = isMoving;
            if (!isMoving)
            {
                currentFrame = 0;
                frameTimer = 0.f;
                return;
            }
            frameTimer += dt;
            if (frameTimer >= frameInterval)
            {
                frameTimer -= frameInterval;
                currentFrame = (currentFrame + 1) % frameCount;
            }
        }
        void applyToSprite(std::optional<sf::Sprite>& spriteOpt, sf::Texture& tex) const
        {
            sf::Vector2f pos = spriteOpt.has_value() ? spriteOpt->getPosition() : sf::Vector2f{};
            sf::Angle rot = spriteOpt.has_value() ? spriteOpt->getRotation() : sf::degrees(0.f);
            sf::Vector2f scl = spriteOpt.has_value() ? spriteOpt->getScale() : sf::Vector2f{ 1.f, 1.f };
            sf::Color col = spriteOpt.has_value() ? spriteOpt->getColor() : sf::Color::White;
            spriteOpt.emplace(tex, currentRect());
            spriteOpt->setOrigin({ frameW / 2.f, frameH / 2.f });
            spriteOpt->setPosition(pos);
            spriteOpt->setRotation(rot);
            spriteOpt->setScale(scl);
            spriteOpt->setColor(col);
        }
        sf::IntRect currentRect() const { return sf::IntRect({ currentFrame * frameW, sheetRow * frameH }, { frameW, frameH }); }
    };

    struct StatusEffect
    {
        EffectType type = EffectType::NONE;
        float duration = 0.f, slowFactor = 0.4f;
        bool active = false;
        void apply(EffectType t, float dur, float slow = 0.4f)
        {
            type = t;
            duration = dur;
            slowFactor = slow;
            active = true;
        }
        void update(float dt)
        {
            if (!active)
                return;
            duration -= dt;
            if (duration <= 0.f)
            {
                type = EffectType::NONE;
                active = false;
            }
        }
        bool isActive() const { return active; }
    };

    struct Bullet
    {
        std::optional<sf::Sprite> sprite;
        sf::Vector2f velocity;
        BulletOwner owner = BulletOwner::PLAYER;
        BulletSpecial special = BulletSpecial::NORMAL;
        float damage = 10.f, speed = 400.f, blastRadius = 0.f, lifetime = 3.f;
        bool active = false, penetrating = false;
        int pierceCount = 0;
        inline static sf::Texture* sharedTexture = nullptr;
        inline static sf::IntRect sharedRect = sf::IntRect({ 0, 0 }, { 0, 0 });
        static void configureSharedSprite(sf::Texture& tex, const sf::IntRect& rect)
        {
            sharedTexture = &tex;
            sharedRect = rect;
        }

        void init(sf::Vector2f pos, sf::Vector2f vel, BulletOwner own, BulletSpecial spec, float dmg, float spd, float radius = 0.f, int pierce = 0)
        {
            if (!sprite.has_value())
                sprite.emplace(*sharedTexture, sharedRect);
            else
            {
                sprite->setTexture(*sharedTexture);
                sprite->setTextureRect(sharedRect);
            }
            sprite->setOrigin({ sharedRect.size.x / 2.f, sharedRect.size.y / 2.f });
            sprite->setPosition(pos);
            velocity = vel;
            owner = own;
            special = spec;
            damage = dmg;
            speed = spd;
            blastRadius = radius;
            pierceCount = pierce;
            penetrating = (pierce > 0);
            active = true;
            lifetime = 3.f;
            colourByType();
        }
        void colourByType()
        {
            sf::Color c = sf::Color::Yellow;
            sf::Vector2f s = { 1.f, 1.f };
            switch (special)
            {
            case BulletSpecial::CHEESE:
                c = sf::Color(255, 220, 0);
                break;
            case BulletSpecial::BAZOOKA:
                c = sf::Color(255, 100, 0);
                s = { 1.5f, 1.5f };
                break;
            case BulletSpecial::REVOLVER:
                c = sf::Color(180, 180, 180);
                s = { 0.9f, 1.25f };
                break;
            case BulletSpecial::MAGIC:
                c = sf::Color(180, 0, 255);
                break;
            case BulletSpecial::ICE:
                c = sf::Color(100, 200, 255);
                break;
            case BulletSpecial::CLUSTER:
                c = sf::Color(255, 60, 0);
                s = { 1.7f, 1.7f };
                break;
            case BulletSpecial::MINI_BAZOOKA:
                c = sf::Color(255, 150, 60);
                s = { 1.35f, 1.35f };
                break;
            case BulletSpecial::MULTI_PENETRATE:
                c = sf::Color(210, 210, 255);
                s = { 1.1f, 1.35f };
                break;
            case BulletSpecial::HEAT_BURST:
                c = sf::Color(255, 140, 0);
                s = { 1.25f, 1.25f };
                break;
            case BulletSpecial::TELEPORT_SHOT:
                c = sf::Color(220, 0, 220);
                s = { 1.2f, 1.2f };
                break;
            default:
                break;
            }
            if (sprite.has_value())
            {
                sprite->setColor(c);
                sprite->setScale(s);
            }
        }
        sf::Vector2f getPosition() const { return sprite.has_value() ? sprite->getPosition() : sf::Vector2f{}; }
        void update(float dt)
        {
            if (!active)
                return;
            if (sprite.has_value())
            {
                sprite->move(velocity * dt);
                float angle = std::atan2(velocity.y, velocity.x) * 180.f / 3.14159f;
                sprite->setRotation(sf::degrees(angle));
            }
            lifetime -= dt;
            if (lifetime <= 0.f)
                active = false;
        }
        void draw(sf::RenderWindow& window) const
        {
            if (!active)
                return;
            if (sprite.has_value())
                window.draw(*sprite);
        }
    };

    struct GroundTrap
    {
        sf::CircleShape shape;
        EffectType effectType = EffectType::STUCK;
        float effectDur = 5.f, lifetime = 8.f;
        bool active = false;
        GroundTrap()
        {
            shape.setRadius(20.f);
            shape.setOrigin({ 20.f, 20.f });
            shape.setFillColor(sf::Color(255, 220, 0, 160));
        }
        void init(sf::Vector2f pos, EffectType et, float eDur, float trapLife, sf::Color col)
        {
            shape.setPosition(pos);
            effectType = et;
            effectDur = eDur;
            lifetime = trapLife;
            active = true;
            shape.setFillColor(col);
        }
        void update(float dt)
        {
            if (!active)
                return;
            lifetime -= dt;
            if (lifetime <= 0.f)
                active = false;
        }
        void draw(sf::RenderWindow& window) const
        {
            if (active)
                window.draw(shape);
        }
    };

    struct AbilityTracker
    {
        int useCheese = 0, useBarooka = 0, useRevolver = 0, useMagic = 0, useIce = 0, totalUses = 0;
        void record(CharacterType cType, BulletSpecial spec)
        {
            totalUses++;
            switch (spec)
            {
            case BulletSpecial::CHEESE:
                useCheese++;
                break;
            case BulletSpecial::BAZOOKA:
                useBarooka++;
                break;
            case BulletSpecial::REVOLVER:
                useRevolver++;
                break;
            case BulletSpecial::MAGIC:
                useMagic++;
                break;
            case BulletSpecial::ICE:
                useIce++;
                break;
            default:
                break;
            }
            (void)cType;
        }
        BulletSpecial dominantSpecial() const
        {
            if (totalUses == 0)
                return BulletSpecial::NORMAL;
            BulletSpecial r = BulletSpecial::CHEESE;
            int m = useCheese;
            if (useBarooka > m)
            {
                m = useBarooka;
                r = BulletSpecial::BAZOOKA;
            }
            if (useRevolver > m)
            {
                m = useRevolver;
                r = BulletSpecial::REVOLVER;
            }
            if (useMagic > m)
            {
                m = useMagic;
                r = BulletSpecial::MAGIC;
            }
            if (useIce > m)
            {
                r = BulletSpecial::ICE;
            }
            return r;
        }
        bool isOneTricking() const
        {
            if (totalUses == 0)
                return false;
            int m = std::max({ useCheese, useBarooka, useRevolver, useMagic, useIce });
            return (float)m / (float)totalUses > 0.5f;
        }
    };

    struct SpecialGun
    {
        float charge = SPECIAL_RECHARGE, maxCharge = SPECIAL_RECHARGE;
        bool isReady() const { return charge >= maxCharge; }
        void recharge(float dt)
        {
            if (charge < maxCharge)
                charge += dt;
            if (charge > maxCharge)
                charge = maxCharge;
        }
        bool consume()
        {
            if (!isReady())
                return false;
            charge = 0.f;
            return true;
        }
        float barFill() const { return charge / maxCharge; }
    };

    inline void applyBulletEffect(StatusEffect& target, BulletSpecial special)
    {
        switch (special)
        {
        case BulletSpecial::CHEESE:
        case BulletSpecial::CHEESE_TRAP:
            target.apply(EffectType::STUCK, 4.f);
            break;
        case BulletSpecial::MAGIC:
            target.apply(EffectType::CONFUSED, 4.f);
            break;
        case BulletSpecial::ICE:
            target.apply(EffectType::SLOWED, 4.f, 0.4f);
            break;
        default:
            break;
        }
    }

    struct Player
    {
        CharacterType type;
        std::string name;
        int playerIndex = 0;
        std::optional<sf::Sprite> sprite;
        sf::Texture* texture = nullptr;
        sf::Vector2f position, aimDir = { 1.f, 0.f };
        float health = PLAYER_MAX_HEALTH, maxHealth = PLAYER_MAX_HEALTH, speed = PLAYER_SPEED;
        bool alive = true;
        int score = 0;
        float mainFireTimer = 0.f;
        SpecialGun special;
        StatusEffect statusEffect;
        SpriteAnimator animator;
        bool isMoving = false;
        Bullet bullets[MAX_BULLETS];
        sf::RectangleShape healthBarBg, healthBarFg, specialBarBg, specialBarFg;
        void init(CharacterType t, sf::Texture& tex, sf::Vector2f startPos, int index = 0)
        {
            type = t;
            playerIndex = index;
            position = startPos;
            health = PLAYER_MAX_HEALTH;
            alive = true;
            score = 0;
            mainFireTimer = 0.f;
            isMoving = false;
            texture = &tex;
            int row = 0;
            switch (t)
            {
            case CharacterType::CHEESE_MAN:
                row = CharacterSheetRow::CHEESE_MAN;
                break;
            case CharacterType::BAZOOKA_MAN:
                row = CharacterSheetRow::BAZOOKA_MAN;
                break;
            case CharacterType::DETECTIVE:
                row = CharacterSheetRow::DETECTIVE;
                break;
            case CharacterType::WIZARD:
                row = CharacterSheetRow::WIZARD;
                break;
            case CharacterType::ICE_MAN:
                row = CharacterSheetRow::ICE_MAN;
                break;
            }
            animator.setup(row, CharacterFrameSize::W, CharacterFrameSize::H, 4, 8.f);
            animator.applyToSprite(sprite, tex);
            switch (t)
            {
            case CharacterType::CHEESE_MAN:
                name = "Cheese Man";
                break;
            case CharacterType::BAZOOKA_MAN:
                name = "Bazooka Man";
                break;
            case CharacterType::DETECTIVE:
                name = "The Detective";
                break;
            case CharacterType::WIZARD:
                name = "The Wizard";
                break;
            case CharacterType::ICE_MAN:
                name = "Ice Man";
                break;
            }
            initHUDShapes();
        }
        void initHUDShapes()
        {
            healthBarBg.setSize({ 200.f, 10.f });
            healthBarBg.setFillColor(sf::Color(80, 0, 0));
            healthBarFg.setSize({ 200.f, 10.f });
            healthBarFg.setFillColor(sf::Color(220, 30, 30));
            specialBarBg.setSize({ 200.f, 6.f });
            specialBarBg.setFillColor(sf::Color(20, 20, 80));
            specialBarFg.setSize({ 200.f, 6.f });
            specialBarFg.setFillColor(sf::Color(80, 80, 255));
        }
        void update(float dt)
        {
            if (!alive)
                return;
            statusEffect.update(dt);
            special.recharge(dt);
            if (mainFireTimer > 0.f)
                mainFireTimer -= dt;
            animator.update(dt, isMoving);
            if (texture)
                animator.applyToSprite(sprite, *texture);
            if (sprite.has_value())
            {
                sprite->setPosition(position);
                sprite->setScale({ 1.7f, 1.7f });
                float angle = std::atan2(aimDir.y, aimDir.x) * 180.f / 3.14159f + PLAYER_SPRITE_ROTATION_OFFSET;
                sprite->setRotation(sf::degrees(angle));
            }
            for (auto& b : bullets)
                b.update(dt);
            float hpFill = std::max(0.f, health / maxHealth);
            healthBarFg.setSize({ 200.f * hpFill, 10.f });
            specialBarFg.setSize({ 200.f * special.barFill(), 6.f });
        }
        void move(sf::Vector2f dir, float dt)
        {
            if (!alive)
                return;
            float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            isMoving = (dirLen > 0.f);
            if (statusEffect.type == EffectType::CONFUSED)
                dir.x = -dir.x;
            float spd = speed;
            if (statusEffect.type == EffectType::STUCK)
                spd = 0.f;
            if (statusEffect.type == EffectType::SLOWED)
                spd *= statusEffect.slowFactor;
            if (statusEffect.type == EffectType::FROZEN)
                spd = 0.f;
            if (spd == 0.f)
                isMoving = false;
            position += dir * spd * dt;
            if (sprite.has_value())
                sprite->setPosition(position);
        }
        void fireMain(float bulletDamage = 15.f, float bulletSpeed = 450.f)
        {
            if (!alive || mainFireTimer > 0.f)
                return;
            mainFireTimer = MAIN_FIRE_RATE;
            for (auto& b : bullets)
            {
                if (!b.active)
                {
                    b.init(position, aimDir * bulletSpeed, BulletOwner::PLAYER, BulletSpecial::NORMAL, bulletDamage, bulletSpeed);
                    break;
                }
            }
        }
        bool fireSpecial(Bullet& outBullet)
        {
            if (!special.consume())
                return false;
            switch (type)
            {
            case CharacterType::CHEESE_MAN:
                outBullet.init(position, aimDir * 350.f, BulletOwner::PLAYER, BulletSpecial::CHEESE, 5.f, 350.f);
                return true;
            case CharacterType::BAZOOKA_MAN:
                outBullet.init(position, aimDir * 250.f, BulletOwner::PLAYER, BulletSpecial::BAZOOKA, 80.f, 250.f, 100.f);
                return true;
            case CharacterType::DETECTIVE:
                outBullet.init(position, aimDir * 600.f, BulletOwner::PLAYER, BulletSpecial::REVOLVER, 40.f, 600.f, 0.f, 3);
                return true;
            case CharacterType::WIZARD:
                outBullet.init(position, aimDir * 400.f, BulletOwner::PLAYER, BulletSpecial::MAGIC, 5.f, 400.f);
                return true;
            case CharacterType::ICE_MAN:
                outBullet.init(position, aimDir * 380.f, BulletOwner::PLAYER, BulletSpecial::ICE, 5.f, 380.f);
                return true;
            }
            return false;
        }
        void takeDamage(float dmg)
        {
            if (!alive)
                return;
            health -= dmg;
            if (health <= 0.f)
            {
                health = 0.f;
                alive = false;
            }
        }
        void applyEffect(EffectType et, float dur, float slow = 0.4f) { statusEffect.apply(et, dur, slow); }
        void draw(sf::RenderWindow& window) const
        {
            if (!alive)
                return;
            if (sprite.has_value())
                window.draw(*sprite);
            for (const auto& b : bullets)
                b.draw(window);
        }
        void drawHUD(sf::RenderWindow& window, sf::Vector2f screenPos)
        {
            healthBarBg.setPosition(screenPos);
            healthBarFg.setPosition(screenPos);
            window.draw(healthBarBg);
            window.draw(healthBarFg);
            sf::Vector2f spPos = screenPos + sf::Vector2f(0.f, 14.f);
            specialBarBg.setPosition(spPos);
            specialBarFg.setPosition(spPos);
            window.draw(specialBarBg);
            window.draw(specialBarFg);
        }
    };

    struct BossAdaptiveOverlord
    {
        std::string name = "The Adaptive Overlord";
        std::optional<sf::Sprite> sprite;
        sf::Texture* texture = nullptr;
        sf::Vector2f position;
        float health = BOSS_MAX_HEALTH;
        float maxHealth = BOSS_MAX_HEALTH;
        float speed = BOSS_SPEED_BASE;
        bool alive = true;
        int phase = 1;
        float resistCheese = 0.f, resistBazooka = 0.f, resistRevolver = 0.f, resistMagic = 0.f, resistIce = 0.f, stealCooldown = 0.f, stealInterval = 5.f, stolenUseTimer = 0.f, reversePlayerTimer = 0.f;
        BulletSpecial stolenAbility = BulletSpecial::NORMAL;
        float fireTimer = 0.f;
        Bullet bullets[MAX_BOSS_BULLETS];
        GroundTrap traps[MAX_GROUND_TRAPS];
        AbilityTracker tracker;
        StatusEffect statusEffect;
        SpriteAnimator animator;
        bool isMoving = false;
        void init(sf::Texture& tex, sf::Vector2f startPos)
        {
            texture = &tex;
            sprite.emplace(tex);
            position = startPos;
            sprite->setPosition(position);
            health = maxHealth;
            alive = true;
            phase = 1;
            speed = BOSS_SPEED_BASE;
            resistCheese = resistBazooka = resistRevolver = resistMagic = resistIce = 0.f;
            stealCooldown = stolenUseTimer = reversePlayerTimer = fireTimer = 0.f;
            stolenAbility = BulletSpecial::NORMAL;
            tracker = AbilityTracker{};
            statusEffect = StatusEffect{};
            isMoving = false;
            animator.setup(BossSheetRow::OVERLORD, BossFrameSize::W, BossFrameSize::H, 7, 10.f);
            animator.applyToSprite(sprite, *texture);
            sprite->setOrigin({ BossFrameSize::W / 2.f, BossFrameSize::H / 2.f });
            for (auto& b : bullets)
                b.active = false;
            for (auto& t : traps)
                t.active = false;
        }
        void takeDamage(float baseDmg, BulletSpecial spec)
        {
            if (!alive)
                return;
            float resist = 0.f;
            switch (spec)
            {
            case BulletSpecial::CHEESE:
                resist = resistCheese;
                break;
            case BulletSpecial::BAZOOKA:
                resist = resistBazooka;
                break;
            case BulletSpecial::REVOLVER:
                resist = resistRevolver;
                break;
            case BulletSpecial::MAGIC:
                resist = resistMagic;
                break;
            case BulletSpecial::ICE:
                resist = resistIce;
                break;
            default:
                break;
            }
            health -= baseDmg * (1.f - resist);
            if (health <= 0.f)
            {
                health = 0.f;
                alive = false;
            }
        }
        void observePlayerSpecial(CharacterType ct, BulletSpecial spec)
        {
            tracker.record(ct, spec);
            switch (tracker.dominantSpecial())
            {
            case BulletSpecial::CHEESE:
                resistCheese = std::min(resistCheese + 0.05f, 0.75f);
                break;
            case BulletSpecial::BAZOOKA:
                resistBazooka = std::min(resistBazooka + 0.05f, 0.75f);
                break;
            case BulletSpecial::REVOLVER:
                resistRevolver = std::min(resistRevolver + 0.05f, 0.75f);
                break;
            case BulletSpecial::MAGIC:
                resistMagic = std::min(resistMagic + 0.05f, 0.75f);
                break;
            case BulletSpecial::ICE:
                resistIce = std::min(resistIce + 0.05f, 0.75f);
                break;
            default:
                break;
            }
        }
        void update(float dt, sf::Vector2f playerPos)
        {
            if (!alive)
                return;
            statusEffect.update(dt);
            fireTimer -= dt;
            stealCooldown -= dt;
            if (stolenUseTimer > 0.f)
                stolenUseTimer -= dt;
            if (stolenUseTimer <= 0.f && phase < 3)
                stolenAbility = BulletSpecial::NORMAL;
            if (reversePlayerTimer > 0.f)
                reversePlayerTimer -= dt;
            float hpRatio = health / maxHealth;
            if (hpRatio <= PHASE3_HP_THRESHOLD && phase < 3)
            {
                phase = 3;
                speed = BOSS_SPEED_BASE * 1.6f;
            }
            else if (hpRatio <= PHASE2_HP_THRESHOLD && phase < 2)
            {
                phase = 2;
                speed = BOSS_SPEED_BASE * 1.2f;
            }
            if (statusEffect.type != EffectType::STUCK && statusEffect.type != EffectType::FROZEN)
            {
                sf::Vector2f dir = playerPos - position;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len > 1.f)
                    dir /= len;
                float spd = speed;
                if (statusEffect.type == EffectType::SLOWED)
                    spd *= statusEffect.slowFactor;
                position += dir * spd * dt;
                if (sprite.has_value())
                    sprite->setPosition(position);
                isMoving = true;
            }
            else
                isMoving = false;
            if (sprite.has_value())
            {
                animator.update(dt, isMoving);
                animator.applyToSprite(sprite, *texture);
                sprite->setPosition(position);
                sf::Vector2f faceDir = playerPos - position;
                float len = std::sqrt(faceDir.x * faceDir.x + faceDir.y * faceDir.y);
                if (len > 1.f)
                    faceDir /= len;
                float angle = std::atan2(faceDir.y, faceDir.x) * 180.f / 3.14159f + BOSS_SPRITE_ROTATION_OFFSET;
                sprite->setRotation(sf::degrees(angle));
            }
            if (phase >= 2 && stealCooldown <= 0.f)
            {
                stealAbility(playerPos);
                stealCooldown = stealInterval;
            }
            if (fireTimer <= 0.f)
            {
                fireBullets(playerPos);
                fireTimer = (phase == 3) ? BOSS_FIRE_INTERVAL * 0.5f : BOSS_FIRE_INTERVAL;
            }
            for (auto& b : bullets)
                b.update(dt);
            for (auto& t : traps)
                t.update(dt);
        }
        void stealAbility(sf::Vector2f)
        {
            stolenAbility = tracker.dominantSpecial();
            stolenUseTimer = 3.f;
            if (stolenAbility == BulletSpecial::MAGIC)
                reversePlayerTimer = 4.f;
        }
        bool isReversingPlayer() const { return reversePlayerTimer > 0.f; }
        void fireBullets(sf::Vector2f playerPos)
        {
            sf::Vector2f dir = playerPos - position;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1.f)
                return;
            dir /= len;
            auto spawnB = [&](BulletSpecial spec, float dmg, float spd, float radius = 0.f, int pierce = 0)
                { for (auto& b : bullets) { if (!b.active) { b.init(position, dir * spd, BulletOwner::BOSS, spec, dmg, spd, radius, pierce); return; } } };
            auto spawnA = [&](BulletSpecial spec, float dmg, float spd, float off, float radius = 0.f, int pierce = 0)
                { sf::Vector2f d = rotateVector(dir, off); for (auto& b : bullets) { if (!b.active) { b.init(position, d * spd, BulletOwner::BOSS, spec, dmg, spd, radius, pierce); return; } } };
            auto spawnT = [&](sf::Vector2f pos, EffectType et, float eDur, float life, sf::Color col)
                { for (auto& t : traps) { if (!t.active) { t.init(pos, et, eDur, life, col); return; } } };
            if (phase == 1)
            {
                spawnB(BulletSpecial::NORMAL, 12.f, 300.f);
            }
            else if (phase == 2)
            {
                switch (stolenAbility)
                {
                case BulletSpecial::CHEESE:
                    spawnT({ position.x + ((rand() % 300) - 150.f), position.y + ((rand() % 300) - 150.f) }, EffectType::STUCK, 5.f, 8.f, sf::Color(255, 220, 0, 180));
                    break;
                case BulletSpecial::BAZOOKA:
                    spawnB(BulletSpecial::MINI_BAZOOKA, 80.f, 250.f, 100.f);
                    break;
                case BulletSpecial::REVOLVER:
                    spawnB(BulletSpecial::REVOLVER, 40.f, 600.f, 0.f, 2);
                    break;
                case BulletSpecial::MAGIC:
                    spawnB(BulletSpecial::MAGIC, 0.f, 300.f);
                    break;
                case BulletSpecial::ICE:
                    spawnB(BulletSpecial::ICE, 0.f, 350.f);
                    break;
                default:
                    spawnB(BulletSpecial::NORMAL, 12.f, 300.f);
                    break;
                }
            }
            else
            {
                spawnA(BulletSpecial::MINI_BAZOOKA, 80.f, 250.f, -0.18f, 50.f);
                spawnA(BulletSpecial::ICE, 0.f, 350.f, 0.00f);
                spawnA(BulletSpecial::MAGIC, 10.f, 320.f, 0.18f);
                spawnT({ position.x + ((rand() % 400) - 200.f), position.y + ((rand() % 400) - 200.f) }, EffectType::STUCK, 4.f, 6.f, sf::Color(255, 220, 0, 150));
            }
        }
        void draw(sf::RenderWindow& window) const
        {
            if (!alive)
                return;
            if (sprite.has_value())
                window.draw(*sprite);
            for (const auto& b : bullets)
                b.draw(window);
            for (const auto& t : traps)
                t.draw(window);
        }
    };

    const int MAX_CLONES = 4, MAX_CLONE_BULLETS = 8;
    struct MirrorClone
    {
        std::optional<sf::Sprite> sprite;
        sf::Vector2f position;
        sf::Vector2f followOffset;
        bool active = false, isReal = false;
        float health = 30.f, fireTimer = 0.f, fireInterval = 2.2f;
        StatusEffect statusEffect;
        Bullet bullets[MAX_CLONE_BULLETS];
        SpriteAnimator animator;
        bool isMoving = false;
        sf::Texture* texture = nullptr;
        void init(sf::Texture* tex, sf::Vector2f pos, bool real, sf::Vector2f offset, sf::Color tint = sf::Color::White)
        {
            texture = tex;
            if (tex)
            {
                sprite.emplace(*tex);
                sprite->setColor(tint);
            }
            else
                sprite.reset();
            position = pos;
            followOffset = offset;
            if (sprite.has_value())
                sprite->setPosition(position);
            active = true;
            isReal = real;
            health = 30.f;
            fireTimer = 0.4f + static_cast<float>(std::rand() % 120) / 100.f;
            statusEffect = StatusEffect{};
            isMoving = false;
            animator.setup(BossSheetRow::MIRROR, BossFrameSize::W, BossFrameSize::H, 7, 10.f);
            if (sprite.has_value())
            {
                animator.applyToSprite(sprite, *texture);
                sprite->setOrigin({ BossFrameSize::W / 2.f, BossFrameSize::H / 2.f });
            }
            for (auto& b : bullets)
                b.active = false;
        }
        void takeDamage(float dmg)
        {
            health -= dmg;
            if (health <= 0.f)
                active = false;
        }
        void syncToBoss(sf::Vector2f bossPos)
        {
            if (!active)
                return;
            if (statusEffect.type != EffectType::STUCK && statusEffect.type != EffectType::FROZEN)
                position = bossPos + followOffset;
            if (sprite.has_value())
                sprite->setPosition(position);
        }
        void update(float dt, sf::Vector2f bossPos, sf::Vector2f playerPos, BulletSpecial copiedAbility, bool hasUpgraded)
        {
            if (!active)
                return;
            statusEffect.update(dt);
            syncToBoss(bossPos);
            if (sprite.has_value())
            {
                isMoving = (statusEffect.type != EffectType::STUCK && statusEffect.type != EffectType::FROZEN);
                animator.update(dt, isMoving);
                animator.applyToSprite(sprite, *texture);
                sf::Vector2f faceDir = playerPos - position;
                float len = std::sqrt(faceDir.x * faceDir.x + faceDir.y * faceDir.y);
                if (len > 1.f)
                    faceDir /= len;
                float angle = std::atan2(faceDir.y, faceDir.x) * 180.f / 3.14159f + BOSS_SPRITE_ROTATION_OFFSET;
                sprite->setRotation(sf::degrees(angle));
            }
            fireTimer -= dt;
            for (auto& b : bullets)
                b.update(dt);
            if (fireTimer <= 0.f)
            {
                fireSupportShot(playerPos, copiedAbility, hasUpgraded);
                fireTimer = fireInterval;
            }
        }
        void fireSupportShot(sf::Vector2f playerPos, BulletSpecial copiedAbility, bool hasUpgraded)
        {
            sf::Vector2f dir = playerPos - position;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1.f)
                return;
            dir /= len;
            BulletSpecial spec = BulletSpecial::NORMAL;
            float dmg = 5.f, spd = 280.f, radius = 0.f;
            int pierce = 0;
            switch (copiedAbility)
            {
            case BulletSpecial::CHEESE:
                spec = BulletSpecial::CHEESE;
                dmg = 3.f;
                spd = 300.f;
                break;
            case BulletSpecial::BAZOOKA:
                spec = hasUpgraded ? BulletSpecial::MINI_BAZOOKA : BulletSpecial::BAZOOKA;
                dmg = hasUpgraded ? 14.f : 10.f;
                spd = 220.f;
                radius = hasUpgraded ? 45.f : 35.f;
                break;
            case BulletSpecial::REVOLVER:
                spec = hasUpgraded ? BulletSpecial::MULTI_PENETRATE : BulletSpecial::REVOLVER;
                dmg = hasUpgraded ? 8.f : 6.f;
                spd = 420.f;
                pierce = hasUpgraded ? 2 : 1;
                break;
            case BulletSpecial::MAGIC:
                spec = BulletSpecial::MAGIC;
                dmg = 2.f;
                spd = 310.f;
                break;
            case BulletSpecial::ICE:
                spec = BulletSpecial::ICE;
                dmg = 2.f;
                spd = 300.f;
                break;
            default:
                break;
            }
            for (auto& b : bullets)
            {
                if (!b.active)
                {
                    b.init(position, dir * spd, BulletOwner::BOSS, spec, dmg, spd, radius, pierce);
                    return;
                }
            }
        }
        sf::Vector2f getPosition() const { return position; }
        void draw(sf::RenderWindow& window) const
        {
            if (!active)
                return;
            if (sprite.has_value())
                window.draw(*sprite);
            for (const auto& b : bullets)
                b.draw(window);
        }
    };

    struct BossMirrorArchitect
    {
        std::string name = "The Mirror Architect";
        std::optional<sf::Sprite> sprite;
        sf::Texture* texture = nullptr;
        sf::Vector2f position;
        float health = BOSS_MAX_HEALTH, maxHealth = BOSS_MAX_HEALTH, speed = BOSS_SPEED_BASE * 0.9f;
        bool alive = true;
        int phase = 1;
        BulletSpecial copiedAbility = BulletSpecial::NORMAL;
        bool hasUpgraded = false;
        int copiedUseCount = 0;
        float copyTimer = 0.f, copyInterval = 10.f;
        MirrorClone clones[MAX_CLONES];
        float cloneSpawnTimer = 7.f, cloneInterval = 15.f;
        GroundTrap distortionZones[MAX_GROUND_TRAPS];
        float fireTimer = 0.f;
        Bullet bullets[MAX_BOSS_BULLETS];
        AbilityTracker tracker;
        StatusEffect statusEffect;
        SpriteAnimator animator;
        bool isMoving = false;
        sf::Vector2f clampPoint(sf::Vector2f pos, sf::Vector2f arenaMin, sf::Vector2f arenaMax) const
        {
            pos.x = std::max(arenaMin.x, std::min(pos.x, arenaMax.x));
            pos.y = std::max(arenaMin.y, std::min(pos.y, arenaMax.y));
            return pos;
        }
        void init(sf::Texture& tex, sf::Texture& /*cloneTex*/, sf::Vector2f startPos)
        {
            texture = &tex;
            sprite.emplace(tex);
            position = startPos;
            sprite->setPosition(position);
            health = maxHealth;
            alive = true;
            phase = 1;
            copiedAbility = BulletSpecial::NORMAL;
            hasUpgraded = false;
            copiedUseCount = 0;
            copyTimer = 0.f;
            cloneSpawnTimer = 7.f;
            fireTimer = 0.f;
            statusEffect = StatusEffect{};
            tracker = AbilityTracker{};
            isMoving = false;
            animator.setup(BossSheetRow::MIRROR, BossFrameSize::W, BossFrameSize::H, 7, 10.f);
            animator.applyToSprite(sprite, *texture);
            sprite->setOrigin({ BossFrameSize::W / 2.f, BossFrameSize::H / 2.f });
            for (auto& b : bullets)
                b.active = false;
            for (auto& z : distortionZones)
                z.active = false;
            for (auto& c : clones)
                c.active = false;
        }
        void observePlayerSpecial(CharacterType ct, BulletSpecial spec)
        {
            tracker.record(ct, spec);
            if (copyTimer <= 0.f)
            {
                copyAbility(spec);
                copyTimer = copyInterval;
            }
        }
        void copyAbility(BulletSpecial spec)
        {
            if (spec == BulletSpecial::NORMAL)
                return;
            copiedAbility = spec;
            hasUpgraded = false;
            copiedUseCount = 0;
        }
        void takeDamage(float dmg, BulletSpecial spec)
        {
            if (!alive)
                return;
            health -= dmg * (tracker.isOneTricking() ? 0.5f : 1.0f);
            if (health <= 0.f)
            {
                health = 0.f;
                alive = false;
            }
        }
        void update(float dt, sf::Vector2f playerPos, sf::Texture& /*cloneTex*/, sf::Vector2f arenaMin, sf::Vector2f arenaMax)
        {
            if (!alive)
                return;
            statusEffect.update(dt);
            fireTimer -= dt;
            copyTimer -= dt;
            cloneSpawnTimer -= dt;
            float hpRatio = health / maxHealth;
            if (hpRatio <= PHASE3_HP_THRESHOLD && phase < 3)
            {
                phase = 3;
                speed = BOSS_SPEED_BASE * 1.45f;
                cloneInterval = 8.f;
            }
            else if (hpRatio <= PHASE2_HP_THRESHOLD && phase < 2)
            {
                phase = 2;
                speed = BOSS_SPEED_BASE * 1.15f;
                cloneInterval = 11.f;
            }
            if (statusEffect.type != EffectType::STUCK && statusEffect.type != EffectType::FROZEN)
            {
                sf::Vector2f dir = playerPos - position;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len > 150.f)
                {
                    dir /= len;
                    float spd = speed;
                    if (statusEffect.type == EffectType::SLOWED)
                        spd *= statusEffect.slowFactor;
                    position += dir * spd * dt;
                    isMoving = true;
                }
                else
                    isMoving = false;
            }
            else
                isMoving = false;
            if (sprite.has_value())
            {
                animator.update(dt, isMoving);
                animator.applyToSprite(sprite, *texture);
                sprite->setPosition(position);
                sf::Vector2f faceDir = playerPos - position;
                float len = std::sqrt(faceDir.x * faceDir.x + faceDir.y * faceDir.y);
                if (len > 1.f)
                    faceDir /= len;
                float angle = std::atan2(faceDir.y, faceDir.x) * 180.f / 3.14159f + BOSS_SPRITE_ROTATION_OFFSET;
                sprite->setRotation(sf::degrees(angle));
            }
            for (auto& c : clones)
                c.syncToBoss(position);
            if (cloneSpawnTimer <= 0.f && phase >= 2)
            {
                spawnClones(arenaMin, arenaMax);
                cloneSpawnTimer = cloneInterval;
            }
            if (fireTimer <= 0.f)
            {
                fireBullets(playerPos, arenaMin, arenaMax);
                fireTimer = (phase == 3) ? BOSS_FIRE_INTERVAL * 0.65f : (phase == 2) ? BOSS_FIRE_INTERVAL * 0.85f
                    : BOSS_FIRE_INTERVAL;
            }
            for (auto& b : bullets)
                b.update(dt);
            for (auto& z : distortionZones)
                z.update(dt);
            for (auto& c : clones)
                c.update(dt, position, playerPos, copiedAbility, hasUpgraded);
        }
        void spawnClones(sf::Vector2f arenaMin, sf::Vector2f arenaMax)
        {
            const sf::Vector2f oldBossPos = position;
            const sf::Vector2f offsets[MAX_CLONES] = { {-120.f, -80.f}, {120.f, -80.f}, {-120.f, 80.f}, {120.f, 80.f} };
            sf::Vector2f spawnPositions[MAX_CLONES];
            for (int i = 0; i < MAX_CLONES; i++)
                spawnPositions[i] = clampPoint(oldBossPos + offsets[i], arenaMin, arenaMax);
            const int bossSlot = std::rand() % MAX_CLONES, realIdx = std::rand() % MAX_CLONES;
            position = spawnPositions[bossSlot];
            if (sprite.has_value())
                sprite->setPosition(position);
            for (int i = 0; i < MAX_CLONES; i++)
            {
                sf::Vector2f clonePos = (i == bossSlot) ? oldBossPos : spawnPositions[i], followOffset = clonePos - position;
                clones[i].init(texture, clonePos, i == realIdx, followOffset, sprite.has_value() ? sprite->getColor() : sf::Color::White);
            }
        }
        void fireBullets(sf::Vector2f playerPos, sf::Vector2f arenaMin, sf::Vector2f arenaMax)
        {
            sf::Vector2f dir = playerPos - position;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1.f)
                return;
            dir /= len;
            auto spawnB = [&](BulletSpecial spec, float dmg, float spd, float radius = 0.f, int pierce = 0)
                { for (auto& b : bullets) { if (!b.active) { b.init(position, dir * spd, BulletOwner::BOSS, spec, dmg, spd, radius, pierce); return; } } };
            if (!hasUpgraded)
            {
                switch (copiedAbility)
                {
                case BulletSpecial::CHEESE:
                    spawnB(BulletSpecial::CHEESE, 5.f, 350.f);
                    break;
                case BulletSpecial::BAZOOKA:
                    spawnB(BulletSpecial::BAZOOKA, 80.f, 250.f, 100.f);
                    break;
                case BulletSpecial::REVOLVER:
                    spawnB(BulletSpecial::REVOLVER, 40.f, 600.f, 0.f, 2);
                    break;
                case BulletSpecial::MAGIC:
                    spawnB(BulletSpecial::MAGIC, 0.f, 400.f);
                    break;
                case BulletSpecial::ICE:
                    spawnB(BulletSpecial::ICE, 0.f, 380.f);
                    break;
                default:
                    spawnB(BulletSpecial::NORMAL, 12.f, 300.f);
                    break;
                }
                if (copiedAbility != BulletSpecial::NORMAL)
                {
                    copiedUseCount++;
                    if (copiedUseCount >= 2)
                        hasUpgraded = true;
                }
            }
            else
            {
                switch (copiedAbility)
                {
                case BulletSpecial::CHEESE:
                    for (int a = -1; a <= 1; a++)
                    {
                        sf::Vector2f sd = { dir.x * std::cos(a * 0.3f) - dir.y * std::sin(a * 0.3f), dir.x * std::sin(a * 0.3f) + dir.y * std::cos(a * 0.3f) };
                        for (auto& b : bullets)
                        {
                            if (!b.active)
                            {
                                b.init(position, sd * 350.f, BulletOwner::BOSS, BulletSpecial::CHEESE, 5.f, 350.f);
                                break;
                            }
                        }
                    }
                    break;
                case BulletSpecial::BAZOOKA:
                    spawnB(BulletSpecial::CLUSTER, 70.f, 240.f, 140.f);
                    break;
                case BulletSpecial::REVOLVER:
                    spawnB(BulletSpecial::MULTI_PENETRATE, 25.f, 600.f, 0.f, 5);
                    break;
                case BulletSpecial::MAGIC:
                    for (auto& z : distortionZones)
                    {
                        if (!z.active)
                        {
                            sf::Vector2f zp = { arenaMin.x + static_cast<float>(std::rand()) / RAND_MAX * (arenaMax.x - arenaMin.x), arenaMin.y + static_cast<float>(std::rand()) / RAND_MAX * (arenaMax.y - arenaMin.y) };
                            z.init(zp, EffectType::CONFUSED, 999.f, 20.f, sf::Color(180, 0, 255, 120));
                            break;
                        }
                    }
                    spawnB(BulletSpecial::MAGIC, 10.f, 400.f);
                    break;
                case BulletSpecial::ICE:
                    for (auto& z : distortionZones)
                    {
                        if (!z.active)
                        {
                            sf::Vector2f zp = { arenaMin.x + static_cast<float>(std::rand()) / RAND_MAX * (arenaMax.x - arenaMin.x), arenaMin.y + static_cast<float>(std::rand()) / RAND_MAX * (arenaMax.y - arenaMin.y) };
                            z.init(zp, EffectType::FROZEN, 3.f, 15.f, sf::Color(100, 200, 255, 140));
                            break;
                        }
                    }
                    spawnB(BulletSpecial::ICE, 10.f, 380.f);
                    break;
                default:
                    spawnB(BulletSpecial::NORMAL, 15.f, 300.f);
                    break;
                }
            }
        }
        void draw(sf::RenderWindow& window) const
        {
            if (!alive)
                return;
            if (sprite.has_value())
                window.draw(*sprite);
            for (const auto& b : bullets)
                b.draw(window);
            for (const auto& z : distortionZones)
                z.draw(window);
            for (const auto& c : clones)
                c.draw(window);
        }
    };

    struct EvolutionMutation
    {
        bool antiStun = false, superSpeed = false, shieldedSpots = false, teleporting = false, heatBursts = false;
        int dnaShiftActive = 0;
        float dnaTimer = 0.f, dnaInterval = 20.f;
    };
    struct BossEvolutionCore
    {
        std::string name = "The Evolution Core";
        std::optional<sf::Sprite> sprite;
        sf::Texture* texture = nullptr;
        sf::Vector2f position;
        float health = BOSS_MAX_HEALTH, maxHealth = BOSS_MAX_HEALTH, speed = BOSS_SPEED_BASE * 0.7f;
        bool alive = true;
        int phase = 1;
        int mutationCount = 0;
        float lastHPMilestone = 1.f;
        EvolutionMutation mutation;
        float heatBurstTimer = 0.f, heatBurstInterval = 8.f, teleportTimer = 0.f, teleportInterval = 6.f;
        sf::CircleShape weakSpot;
        bool weakSpotVisible = true;
        float fireTimer = 0.f;
        Bullet bullets[MAX_BOSS_BULLETS];
        Bullet heatRings[10];
        AbilityTracker tracker;
        StatusEffect statusEffect;
        bool effectBlocked = false;
        SpriteAnimator animator;
        bool isMoving = false;
        void init(sf::Texture& tex, sf::Vector2f startPos)
        {
            texture = &tex;
            sprite.emplace(tex);
            position = startPos;
            sprite->setPosition(position);
            health = maxHealth;
            alive = true;
            phase = 1;
            mutationCount = 0;
            lastHPMilestone = 1.f;
            weakSpot.setRadius(12.f);
            weakSpot.setOrigin({ 12.f, 12.f });
            weakSpot.setFillColor(sf::Color(255, 50, 50, 200));
            mutation = EvolutionMutation{};
            heatBurstTimer = teleportTimer = fireTimer = 0.f;
            weakSpotVisible = true;
            effectBlocked = false;
            statusEffect = StatusEffect{};
            tracker = AbilityTracker{};
            isMoving = false;
            animator.setup(BossSheetRow::EVOLUTION, BossFrameSize::W, BossFrameSize::H, 7, 10.f);
            animator.applyToSprite(sprite, *texture);
            sprite->setOrigin({ BossFrameSize::W / 2.f, BossFrameSize::H / 2.f });
            for (auto& b : bullets)
                b.active = false;
            for (auto& r : heatRings)
                r.active = false;
        }
        void takeDamage(float dmg, CharacterType charType, BulletSpecial spec, sf::Vector2f hitPos)
        {
            if (!alive)
                return;
            if (mutation.antiStun && spec == BulletSpecial::CHEESE)
            {
                effectBlocked = true;
                return;
            }
            effectBlocked = false;
            if (mutation.shieldedSpots && spec == BulletSpecial::REVOLVER)
            {
                sf::Vector2f ws = position + sf::Vector2f(0.f, -20.f);
                float dx = hitPos.x - ws.x, dy = hitPos.y - ws.y;
                if (std::sqrt(dx * dx + dy * dy) > 14.f)
                    dmg *= 0.1f;
            }
            if (!mutation.shieldedSpots)
            {
                sf::Vector2f ws = position + sf::Vector2f(0.f, -20.f);
                float dx = hitPos.x - ws.x, dy = hitPos.y - ws.y;
                if (std::sqrt(dx * dx + dy * dy) <= 14.f)
                    dmg *= 2.f;
            }
            health -= dmg;
            if (health <= 0.f)
            {
                health = 0.f;
                alive = false;
                return;
            }
            tracker.record(charType, spec);
            checkMutation();
        }
        void checkMutation()
        {
            float nextMilestone = lastHPMilestone - 0.2f;
            if (health / maxHealth <= nextMilestone && mutationCount < 5)
            {
                mutationCount++;
                lastHPMilestone = nextMilestone;
                triggerMutation();
            }
        }
        void triggerMutation()
        {
            switch (tracker.dominantSpecial())
            {
            case BulletSpecial::CHEESE:
                mutation.antiStun = true;
                if (sprite.has_value())
                    sprite->setColor(sf::Color(255, 220, 100));
                break;
            case BulletSpecial::BAZOOKA:
                mutation.superSpeed = true;
                speed = BOSS_SPEED_BASE * 2.0f;
                if (sprite.has_value())
                    sprite->setColor(sf::Color(255, 120, 0));
                break;
            case BulletSpecial::REVOLVER:
                mutation.shieldedSpots = true;
                weakSpotVisible = false;
                if (sprite.has_value())
                    sprite->setColor(sf::Color(150, 150, 150));
                break;
            case BulletSpecial::MAGIC:
                mutation.teleporting = true;
                teleportTimer = teleportInterval;
                if (sprite.has_value())
                    sprite->setColor(sf::Color(180, 0, 255));
                break;
            case BulletSpecial::ICE:
                mutation.heatBursts = true;
                heatBurstTimer = heatBurstInterval;
                if (sprite.has_value())
                    sprite->setColor(sf::Color(255, 80, 0));
                break;
            default:
                break;
            }
            if (mutationCount == 3 || mutationCount == 5)
            {
                mutation.dnaShiftActive = 2;
                mutation.dnaTimer = mutation.dnaInterval;
            }
        }
        void update(float dt, sf::Vector2f playerPos, sf::Vector2f arenaMin, sf::Vector2f arenaMax)
        {
            if (!alive)
                return;
            statusEffect.update(dt);
            fireTimer -= dt;
            mutation.dnaTimer -= dt;
            float hpRatio = health / maxHealth;
            if (hpRatio <= PHASE3_HP_THRESHOLD && phase < 3)
                phase = 3;
            else if (hpRatio <= PHASE2_HP_THRESHOLD && phase < 2)
                phase = 2;
            if (statusEffect.type != EffectType::STUCK && statusEffect.type != EffectType::FROZEN)
            {
                sf::Vector2f dir = playerPos - position;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len > 1.f)
                    dir /= len;
                float spd = speed;
                if (statusEffect.type == EffectType::SLOWED)
                    spd *= statusEffect.slowFactor;
                position += dir * spd * dt;
                isMoving = true;
            }
            else
                isMoving = false;
            if (mutation.teleporting)
            {
                teleportTimer -= dt;
                if (teleportTimer <= 0.f)
                {
                    position = { arenaMin.x + (float)rand() / RAND_MAX * (arenaMax.x - arenaMin.x), arenaMin.y + (float)rand() / RAND_MAX * (arenaMax.y - arenaMin.y) };
                    teleportTimer = teleportInterval;
                    sf::Vector2f dir = playerPos - position;
                    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    if (len > 1.f)
                        dir /= len;
                    for (auto& b : bullets)
                    {
                        if (!b.active)
                        {
                            b.init(position, dir * 420.f, BulletOwner::BOSS, BulletSpecial::TELEPORT_SHOT, 18.f, 420.f);
                            break;
                        }
                    }
                }
            }
            if (sprite.has_value())
            {
                animator.update(dt, isMoving);
                animator.applyToSprite(sprite, *texture);
                sprite->setPosition(position);
                sf::Vector2f faceDir = playerPos - position;
                float flen = std::sqrt(faceDir.x * faceDir.x + faceDir.y * faceDir.y);
                if (flen > 1.f)
                    faceDir /= flen;
                float angle = std::atan2(faceDir.y, faceDir.x) * 180.f / 3.14159f + BOSS_SPRITE_ROTATION_OFFSET;
                sprite->setRotation(sf::degrees(angle));
            }
            weakSpot.setPosition(position + sf::Vector2f(0.f, -20.f));
            if (mutation.heatBursts)
            {
                heatBurstTimer -= dt;
                if (heatBurstTimer <= 0.f)
                {
                    spawnHeatRing();
                    heatBurstTimer = heatBurstInterval;
                }
            }
            if (fireTimer <= 0.f)
            {
                fireBullets(playerPos);
                fireTimer = (phase >= 3) ? BOSS_FIRE_INTERVAL * 0.6f : BOSS_FIRE_INTERVAL;
            }
            for (auto& b : bullets)
                b.update(dt);
            for (auto& r : heatRings)
                r.update(dt);
        }
        void fireBullets(sf::Vector2f playerPos)
        {
            sf::Vector2f dir = playerPos - position;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1.f)
                return;
            dir /= len;
            auto spawnA = [&](BulletSpecial spec, float dmg, float spd, float off, float radius = 0.f, int pierce = 0)
                { sf::Vector2f d = rotateVector(dir, off); for (auto& b : bullets) { if (!b.active) { b.init(position, d * spd, BulletOwner::BOSS, spec, dmg, spd, radius, pierce); return; } } };
            spawnA(BulletSpecial::NORMAL, 10.f + mutationCount * 2.f, 300.f, 0.f);
            if (mutation.dnaShiftActive > 0 && mutation.dnaTimer > 0.f)
            {
                spawnA(BulletSpecial::ICE, 8.f, 350.f, -0.14f);
                spawnA(BulletSpecial::BAZOOKA, 25.f, 200.f, 0.14f, 70.f);
            }
        }
        void spawnHeatRing()
        {
            for (int i = 0; i < 8; i++)
            {
                float angle = i * 45.f * 3.14159f / 180.f;
                sf::Vector2f dir = { std::cos(angle), std::sin(angle) };
                for (auto& r : heatRings)
                {
                    if (!r.active)
                    {
                        r.init(position, dir * 280.f, BulletOwner::BOSS, BulletSpecial::HEAT_BURST, 15.f, 280.f);
                        break;
                    }
                }
            }
        }
        void draw(sf::RenderWindow& window) const
        {
            if (!alive)
                return;
            if (sprite.has_value())
                window.draw(*sprite);
            if (weakSpotVisible && !mutation.shieldedSpots)
                window.draw(weakSpot);
            for (const auto& b : bullets)
                b.draw(window);
            for (const auto& r : heatRings)
                r.draw(window);
        }
    };

    // Boss mode setup
    const unsigned int WIN_W = 1920, WIN_H = 1080;
    const sf::Vector2f ARENA_MIN = { 50.f, 50.f }, ARENA_MAX = { static_cast<float>(WIN_W) - 50.f, static_cast<float>(WIN_H) - 50.f };
    const sf::IntRect BULLET_FRAME(sf::Vector2i(0, 0), sf::Vector2i(16, 16));
    const sf::IntRect FLOOR_TILE_FRAME(sf::Vector2i(426, 41), sf::Vector2i(109, 109));
    enum class ActiveBoss
    {
        OVERLORD,
        MIRROR,
        EVOLUTION
    };
    struct TextureAssets
    {
        sf::Texture BulletSheet;
        sf::Texture floorTexture;
        sf::Texture characterSheet;
        sf::Texture bossSheet;
    };
    struct SceneState
    {
        std::vector<sf::Sprite> floorTiles;
        sf::RectangleShape arenaBorder;
    };
    struct HudState
    {
        sf::Font font;
        bool hasFont = false;
        std::optional<sf::Text> hudText;
    };
    struct BossRoster
    {
        BossAdaptiveOverlord overlord;
        BossMirrorArchitect mirror;
        BossEvolutionCore evolution;
        ActiveBoss activeBoss = ActiveBoss::OVERLORD;
    };
    struct GameState
    {
        CharacterType playerType = CharacterType::WIZARD;
        Player player;
        BossRoster bosses;
    };
    struct FrameState
    {
        float dt = 0.f;
        sf::Vector2i mousePixel = { 0, 0 };
        sf::Vector2f mouseWorld = { 0.f, 0.f };
        sf::Vector2f moveDir = { 0.f, 0.f };
    };
    struct BossHudInfo
    {
        float health = 0.f;
        float maxHealth = BOSS_MAX_HEALTH;
        std::string name;
        const StatusEffect* statusEffect = nullptr;
    };
    struct GameContext
    {
        sf::RenderWindow& window;
        int& menucounter;
        sf::Clock clock;
        TextureAssets assets;
        SceneState scene;
        HudState hud;
        GameState state;
        GameContext(sf::RenderWindow& w, int& mc) : window(w), menucounter(mc) {}
    };
    void clampToArena(sf::Vector2f& position)
    {
        if (position.x < ARENA_MIN.x)
            position.x = ARENA_MIN.x;
        if (position.y < ARENA_MIN.y)
            position.y = ARENA_MIN.y;
        if (position.x > ARENA_MAX.x)
            position.x = ARENA_MAX.x;
        if (position.y > ARENA_MAX.y)
            position.y = ARENA_MAX.y;
    }

    bool loadBossTextures(TextureAssets& assets)
    {
        if (!loadTextureSafe(assets.BulletSheet, "Bullet.png"))
            std::cerr << "Warning: Bullet sheet not found\n";
        if (!loadTextureSafe(assets.floorTexture, "floor.png"))
            std::cerr << "Warning: Floor texture not found\n";
        if (!loadTextureSafe(assets.characterSheet, "characters.png"))
            std::cerr << "Warning: Characters sheet not found\n";
        if (!loadTextureSafe(assets.bossSheet, "Bosses.png"))
            std::cerr << "Warning: Boss sheet not found\n";
        Bullet::configureSharedSprite(assets.BulletSheet, BULLET_FRAME);
        return true;
    }

    void initializeScene(SceneState& scene, const TextureAssets& assets)
    {
        scene.floorTiles.reserve(((WIN_W / FLOOR_TILE_FRAME.size.x) + 2) * ((WIN_H / FLOOR_TILE_FRAME.size.y) + 2));
        for (int y = 0; y < static_cast<int>(WIN_H); y += FLOOR_TILE_FRAME.size.y)
        {
            for (int x = 0; x < static_cast<int>(WIN_W); x += FLOOR_TILE_FRAME.size.x)
            {
                sf::Sprite tile(assets.floorTexture, FLOOR_TILE_FRAME);
                tile.setPosition({ static_cast<float>(x), static_cast<float>(y) });
                scene.floorTiles.push_back(tile);
            }
        }
        scene.arenaBorder.setSize({ ARENA_MAX.x - ARENA_MIN.x, ARENA_MAX.y - ARENA_MIN.y });
        scene.arenaBorder.setPosition(ARENA_MIN);
        scene.arenaBorder.setFillColor(sf::Color::Transparent);
        scene.arenaBorder.setOutlineColor(sf::Color(100, 100, 100));
        scene.arenaBorder.setOutlineThickness(2.f);
    }

    void initializeHud(HudState& hud)
    {
        const std::array<const char*, 2> fontCandidates = { "Star_Crush.ttf", "arial.ttf" };
        for (const char* path : fontCandidates)
        {
            if (openFontSafe(hud.font, path))
            {
                hud.hasFont = true;
                break;
            }
        }
        if (hud.hasFont)
        {
            hud.hudText.emplace(hud.font, "", 24);
            hud.hudText->setFillColor(sf::Color::White);
        }
    }

    void resetOverlord(BossRoster& bosses, sf::Texture& bossSheet) { bosses.overlord.init(bossSheet, { 200.f, 200.f }); }
    void resetMirror(BossRoster& bosses, sf::Texture& bossSheet) { bosses.mirror.init(bossSheet, bossSheet, { 900.f, 200.f }); }
    void resetEvolution(BossRoster& bosses, sf::Texture& bossSheet) { bosses.evolution.init(bossSheet, { 600.f, 150.f }); }

    void setActiveBoss(BossRoster& bosses, ActiveBoss activeBoss, sf::Texture& bossSheet)
    {
        bosses.activeBoss = activeBoss;
        switch (activeBoss)
        {
        case ActiveBoss::OVERLORD:
            resetOverlord(bosses, bossSheet);
            break;
        case ActiveBoss::MIRROR:
            resetMirror(bosses, bossSheet);
            break;
        case ActiveBoss::EVOLUTION:
            resetEvolution(bosses, bossSheet);
            break;
        }
    }

    bool initializeGame(GameContext& game)
    {
        loadBossTextures(game.assets);
        initializeScene(game.scene, game.assets);
        initializeHud(game.hud);
        game.state.player.init(game.state.playerType, game.assets.characterSheet, { WIN_W / 2.f, WIN_H / 2.f });
        resetOverlord(game.state.bosses, game.assets.bossSheet);
        resetMirror(game.state.bosses, game.assets.bossSheet);
        resetEvolution(game.state.bosses, game.assets.bossSheet);
        game.state.bosses.activeBoss = ActiveBoss::OVERLORD;
        return true;
    }

    FrameState beginFrame(GameContext& game)
    {
        FrameState frame;
        frame.dt = game.clock.restart().asSeconds();
        if (frame.dt > 0.05f)
            frame.dt = 0.05f;
        frame.mousePixel = sf::Mouse::getPosition(game.window);
        frame.mouseWorld = mapPixelToVirtualScreen(game.window, frame.mousePixel);
        return frame;
    }

    sf::Vector2f readMovementInput()
    {
        sf::Vector2f moveDir = { 0.f, 0.f };
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W))
            moveDir.y -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S))
            moveDir.y += 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A))
            moveDir.x -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D))
            moveDir.x += 1.f;
        float length = std::sqrt(moveDir.x * moveDir.x + moveDir.y * moveDir.y);
        if (length > 0.f)
            moveDir /= length;
        return moveDir;
    }

    template <typename DamageFn, typename EffectFn>
    void resolveBulletHitOnBoss(Bullet& bullet, sf::Vector2f bossPosition, DamageFn applyDamage, EffectFn applyEffect)
    {
        bool directHit = bullet.active && isWithinRadius(bullet.getPosition(), bossPosition, 35.f);
        bool blastHit = !directHit && bullet.blastRadius > 0.f && isWithinRadius(bullet.getPosition(), bossPosition, bullet.blastRadius);
        if (!directHit && !blastHit)
            return;
        float damage = directHit ? bullet.damage : bullet.damage * 0.6f;
        applyDamage(damage, bullet.special);
        applyEffect(bullet.special);
        if (bullet.penetrating && bullet.pierceCount > 0)
        {
            bullet.pierceCount--;
            if (bullet.pierceCount <= 0)
                bullet.active = false;
            return;
        }
        bullet.active = false;
    }

    void drawTextLine(sf::RenderWindow& window, sf::Text& text, const std::string& content, sf::Vector2f position, sf::Color color = sf::Color::White)
    {
        text.setFillColor(color);
        text.setString(content);
        text.setPosition(position);
        window.draw(text);
    }

    void drawBossHealthBar(sf::RenderWindow& window, const BossHudInfo& bossHud)
    {
        const float bossHudWidth = 600.f;
        const float bossHudX = (WIN_W - bossHudWidth) / 2.f;
        sf::RectangleShape bossBg({ bossHudWidth, 20.f });
        bossBg.setPosition({ bossHudX, 20.f });
        bossBg.setFillColor(sf::Color(60, 0, 0));
        window.draw(bossBg);
        float healthRatio = (bossHud.maxHealth > 0.f) ? (bossHud.health / bossHud.maxHealth) : 0.f;
        sf::RectangleShape bossFg({ bossHudWidth * healthRatio, 20.f });
        bossFg.setPosition({ bossHudX, 20.f });
        bossFg.setFillColor(sf::Color(220, 30, 30));
        window.draw(bossFg);
    }

    CharacterType chooseBossCharacter(sf::RenderWindow& window, int& menucounter)
    {
        sf::Font font;
        if (!openFontSafe(font, "Pixelmax-Regular.otf") && !openFontSafe(font, "arial.ttf"))
            return CharacterType::WIZARD;

        const std::array<CharacterType, 5> types = {
            CharacterType::CHEESE_MAN,
            CharacterType::BAZOOKA_MAN,
            CharacterType::DETECTIVE,
            CharacterType::WIZARD,
            CharacterType::ICE_MAN
        };
        const std::array<std::string, 5> names = {
            std::string("Cheese Man"),
            std::string("Bazooka Man"),
            std::string("The Detective"),
            std::string("The Wizard"),
            std::string("Ice Man")
        };

        std::vector<sf::Text> optionText;
        optionText.reserve(5);
        for (int i = 0; i < 5; ++i)
        {
            sf::Text option(font, std::to_string(i + 1) + ". " + names[i], 42);
            option.setFillColor(sf::Color::White);
            option.setPosition({ 720.f, 260.f + static_cast<float>(i) * 70.f });
            optionText.push_back(option);
        }

        sf::Text title(font, "Choose Your Character", 72);
        title.setFillColor(sf::Color::White);
        title.setPosition({ 680.f, 120.f });

        sf::Text info(font, "Use 1-5 or Arrow keys. Enter to start.", 28);
        info.setFillColor(sf::Color(200, 200, 255));
        info.setPosition({ 720.f, 760.f });

        int selectedIndex = 3;
        while (window.isOpen())
        {
            while (const std::optional<sf::Event> event = window.pollEvent())
            {
                if (event->is<sf::Event::Closed>())
                {
                    window.close();
                    return types[selectedIndex];
                }
                if (const auto* keyEvent = event->getIf<sf::Event::KeyPressed>())
                {
                    if (keyEvent->code == sf::Keyboard::Key::Escape)
                    {
                        menucounter = 6;
                        return types[selectedIndex];
                    }
                    if (keyEvent->code >= sf::Keyboard::Key::Num1 && keyEvent->code <= sf::Keyboard::Key::Num5)
                    {
                        selectedIndex = static_cast<int>(keyEvent->code) - static_cast<int>(sf::Keyboard::Key::Num1);
                    }
                    else if (keyEvent->code == sf::Keyboard::Key::Left)
                    {
                        selectedIndex = (selectedIndex + 4) % 5;
                    }
                    else if (keyEvent->code == sf::Keyboard::Key::Right)
                    {
                        selectedIndex = (selectedIndex + 1) % 5;
                    }
                    else if (keyEvent->code == sf::Keyboard::Key::Enter)
                    {
                        return types[selectedIndex];
                    }
                }
            }

            for (int i = 0; i < 5; ++i)
                optionText[i].setFillColor(i == selectedIndex ? sf::Color::Cyan : sf::Color::White);

            window.clear(sf::Color(30, 30, 35));
            window.setView(makeVirtualScreenView(window));
            window.draw(title);
            for (int i = 0; i < 5; ++i)
                window.draw(optionText[i]);
            window.draw(info);
            window.display();
        }

        return types[selectedIndex];
    }

    void runBossGame(sf::RenderWindow& window, int& menucounter)
    {
        GameContext game(window, menucounter);
        game.state.playerType = chooseBossCharacter(window, menucounter);
        if (menucounter != 20)
            return;
        initializeGame(game);
        bool gameRunning = true;
        while (window.isOpen() && gameRunning)
        {
            FrameState frame = beginFrame(game);
            frame.moveDir = readMovementInput();
            while (const std::optional<sf::Event> event = game.window.pollEvent())
            {
                if (event->is<sf::Event::Closed>())
                {
                    game.window.close();
                    return;
                }
                if (const auto* keyEvent = event->getIf<sf::Event::KeyPressed>())
                {
                    if (keyEvent->code == sf::Keyboard::Key::Escape)
                    {
                        gameRunning = false;
                        menucounter = 6;
                        break;
                    }
                    if (keyEvent->code == sf::Keyboard::Key::Num1)
                        setActiveBoss(game.state.bosses, ActiveBoss::OVERLORD, game.assets.bossSheet);
                    if (keyEvent->code == sf::Keyboard::Key::Num2)
                        setActiveBoss(game.state.bosses, ActiveBoss::MIRROR, game.assets.bossSheet);
                    if (keyEvent->code == sf::Keyboard::Key::Num3)
                        setActiveBoss(game.state.bosses, ActiveBoss::EVOLUTION, game.assets.bossSheet);
                }
                if (const auto* mouseEvent = event->getIf<sf::Event::MouseButtonPressed>())
                {
                    if (mouseEvent->button == sf::Mouse::Button::Right)
                    {
                        Bullet specialBullet;
                        game.state.player.aimDir = aimAt(game.state.player.position, frame.mouseWorld);
                        if (game.state.player.fireSpecial(specialBullet))
                        {
                            for (auto& pb : game.state.player.bullets)
                            {
                                if (!pb.active)
                                {
                                    pb = specialBullet;
                                    break;
                                }
                            }
                            game.state.bosses.overlord.observePlayerSpecial(game.state.player.type, specialBullet.special);
                            game.state.bosses.mirror.observePlayerSpecial(game.state.player.type, specialBullet.special);
                            game.state.bosses.evolution.tracker.record(game.state.player.type, specialBullet.special);
                        }
                    }
                }
            }
            if (!gameRunning)
                break;

            game.state.player.aimDir = aimAt(game.state.player.position, frame.mouseWorld);
            game.state.player.move(frame.moveDir, frame.dt);
            clampToArena(game.state.player.position);
            if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left))
                game.state.player.fireMain();
            if (game.state.bosses.activeBoss == ActiveBoss::OVERLORD && game.state.bosses.overlord.isReversingPlayer())
                game.state.player.applyEffect(EffectType::CONFUSED, 0.1f);
            game.state.player.update(frame.dt);

            switch (game.state.bosses.activeBoss)
            {
            case ActiveBoss::OVERLORD:
                game.state.bosses.overlord.update(frame.dt, game.state.player.position);
                break;
            case ActiveBoss::MIRROR:
                game.state.bosses.mirror.update(frame.dt, game.state.player.position, game.assets.BulletSheet, ARENA_MIN, ARENA_MAX);
                break;
            case ActiveBoss::EVOLUTION:
                game.state.bosses.evolution.update(frame.dt, game.state.player.position, ARENA_MIN, ARENA_MAX);
                break;
            }

            for (auto& bullet : game.state.player.bullets)
            {
                if (!bullet.active)
                    continue;
                switch (game.state.bosses.activeBoss)
                {
                case ActiveBoss::OVERLORD:
                    resolveBulletHitOnBoss(bullet, game.state.bosses.overlord.position, [&](float dmg, BulletSpecial sp)
                        {game.state.bosses.overlord.takeDamage(dmg, sp); game.state.bosses.overlord.observePlayerSpecial(game.state.player.type, sp); }, [&](BulletSpecial sp)
                        { applyBulletEffect(game.state.bosses.overlord.statusEffect, sp); });
                    break;
                case ActiveBoss::MIRROR:
                    resolveBulletHitOnBoss(bullet, game.state.bosses.mirror.position, [&](float dmg, BulletSpecial sp)
                        { game.state.bosses.mirror.takeDamage(dmg, sp); }, [&](BulletSpecial sp)
                        { applyBulletEffect(game.state.bosses.mirror.statusEffect, sp); });
                    for (int i = 0; i < MAX_CLONES; i++)
                    {
                        if (game.state.bosses.mirror.clones[i].active && bullet.active && isWithinRadius(bullet.getPosition(), game.state.bosses.mirror.clones[i].getPosition(), 35.f))
                        {
                            game.state.bosses.mirror.clones[i].takeDamage(bullet.damage);
                            if (!bullet.penetrating)
                                bullet.active = false;
                            break;
                        }
                    }
                    break;
                case ActiveBoss::EVOLUTION:
                    resolveBulletHitOnBoss(bullet, game.state.bosses.evolution.position, [&](float dmg, BulletSpecial sp)
                        { game.state.bosses.evolution.takeDamage(dmg, game.state.player.type, sp, bullet.getPosition()); }, [&](BulletSpecial sp)
                        {if (!game.state.bosses.evolution.effectBlocked) applyBulletEffect(game.state.bosses.evolution.statusEffect, sp); });
                    break;
                }
            }

            auto checkB = [&](Bullet* bArr, int c)
                { for (int i = 0; i < c; i++) { if (bArr[i].active && isWithinRadius(bArr[i].getPosition(), game.state.player.position, 24.f)) { game.state.player.takeDamage(bArr[i].damage); applyBulletEffect(game.state.player.statusEffect, bArr[i].special); bArr[i].active = false; } } };
            switch (game.state.bosses.activeBoss)
            {
            case ActiveBoss::OVERLORD:
                checkB(game.state.bosses.overlord.bullets, MAX_BOSS_BULLETS);
                for (auto& t : game.state.bosses.overlord.traps)
                    if (t.active && isWithinRadius(t.shape.getPosition(), game.state.player.position, 28.f))
                        game.state.player.applyEffect(t.effectType, t.effectDur);
                break;
            case ActiveBoss::MIRROR:
                checkB(game.state.bosses.mirror.bullets, MAX_BOSS_BULLETS);
                for (int i = 0; i < MAX_CLONES; i++)
                {
                    if (game.state.bosses.mirror.clones[i].active)
                    {
                        checkB(game.state.bosses.mirror.clones[i].bullets, MAX_CLONE_BULLETS);
                        if (game.state.bosses.mirror.clones[i].isReal && isWithinRadius(game.state.bosses.mirror.clones[i].getPosition(), game.state.player.position, 28.f))
                            game.state.player.takeDamage(8.f);
                    }
                }
                for (auto& z : game.state.bosses.mirror.distortionZones)
                    if (z.active && isWithinRadius(z.shape.getPosition(), game.state.player.position, 28.f))
                        game.state.player.applyEffect(z.effectType, 0.1f);
                break;
            case ActiveBoss::EVOLUTION:
                checkB(game.state.bosses.evolution.bullets, MAX_BOSS_BULLETS);
                checkB(game.state.bosses.evolution.heatRings, 10);
                break;
            }

            if (!game.state.player.alive)
            {
                std::cout << "Player died! Returning to menu...\n";
                gameRunning = false;
                menucounter = 6;
            }

            BossHudInfo bossHud;
            switch (game.state.bosses.activeBoss)
            {
            case ActiveBoss::OVERLORD:
                bossHud.health = game.state.bosses.overlord.health;
                bossHud.name = game.state.bosses.overlord.name + "  Phase " + std::to_string(game.state.bosses.overlord.phase);
                bossHud.statusEffect = &game.state.bosses.overlord.statusEffect;
                break;
            case ActiveBoss::MIRROR:
                bossHud.health = game.state.bosses.mirror.health;
                bossHud.name = game.state.bosses.mirror.name + "  Phase " + std::to_string(game.state.bosses.mirror.phase);
                bossHud.statusEffect = &game.state.bosses.mirror.statusEffect;
                break;
            case ActiveBoss::EVOLUTION:
                bossHud.health = game.state.bosses.evolution.health;
                bossHud.name = game.state.bosses.evolution.name + "  Mutations:" + std::to_string(game.state.bosses.evolution.mutationCount);
                bossHud.statusEffect = &game.state.bosses.evolution.statusEffect;
                break;
            }

            window.clear(sf::Color(30, 30, 35));
            window.setView(makeVirtualScreenView(window));
            for (const auto& tile : game.scene.floorTiles)
                window.draw(tile);
            window.draw(game.scene.arenaBorder);
            switch (game.state.bosses.activeBoss)
            {
            case ActiveBoss::OVERLORD:
                game.state.bosses.overlord.draw(window);
                break;
            case ActiveBoss::MIRROR:
                game.state.bosses.mirror.draw(window);
                break;
            case ActiveBoss::EVOLUTION:
                game.state.bosses.evolution.draw(window);
                break;
            }
            game.state.player.draw(window);
            game.state.player.drawHUD(window, { 20.f, 20.f });
            drawBossHealthBar(window, bossHud);

            if (game.hud.hasFont && game.hud.hudText.has_value())
            {
                sf::Text& ht = *game.hud.hudText;
                drawTextLine(window, ht, "Boss HP: " + std::to_string((int)bossHud.health) + "/" + std::to_string((int)bossHud.maxHealth), { WIN_W / 2.f - 100.f, 0.f });
                drawTextLine(window, ht, bossHud.name, { WIN_W / 2.f - 100.f, 45.f });
                drawTextLine(window, ht, "Player HP: " + std::to_string((int)game.state.player.health), { 230.f, 12.f });
                drawTextLine(window, ht, "Special: " + std::to_string((int)(game.state.player.special.barFill() * 100.f)) + "%", { 230.f, 28.f });
                drawTextLine(window, ht, "WASD: Move  |  LMB: Shoot  |  RMB: Special  |  1/2/3: Switch Boss", { 20.f, WIN_H - 30.f });
                ht.setFillColor(sf::Color::White);
            }
            window.display();
        }
    }
} // end BossMode

// ====================================================================================
// ============================== ONLINE MODES (TDM & CAPTURE) ========================
// ====================================================================================
namespace OnlineMode
{
    constexpr int MAP_HEIGHT = 52, MAP_WIDTH = 52;
    constexpr float TILE_SIZE = 60.f;
    const int path_tile = 0, wall = 1, brokenwall = 2, empty_entity = 0, spawn_point = 1, heal = 2, powerup_maxhp = 3, barrel = 4, explodedbarrel = 5, base_station = 6, powerup_speed = 7, powerup_damage = 8;
    int gameMap[MAP_HEIGHT][MAP_WIDTH][2];
    std::vector<sf::Vector2f> spawnPoints;

    enum class PacketType
    {
        Init,
        PlayerUpdate,
        BulletFired,
        Disconnect,
        PlayerDied,
        ScoreUpdate,
        GameOver,
        BarrelExploded,
        UdpHandshake,
        BaseCaptured
    };

    struct BaseStation
    {
        sf::RectangleShape shape;
        float captureProgress = 0.f;
        bool isCaptured = false;
    };
    struct MapTextureSet
    {
        sf::Texture floors[3];
        sf::Texture wall, brokenWall, heal, spawn, barrel, explodedBarrel;
    };
    MapTextureSet mapTextures;
    std::array<std::array<int, MAP_WIDTH>, MAP_HEIGHT> floorTextureIndices{};

    bool isTextureReady(const sf::Texture& texture) { return texture.getSize().x > 0 && texture.getSize().y > 0; }

    bool loadMapTextures()
    {
        bool ok = true;
        ok &= loadTextureSafe(mapTextures.floors[0], "floorone.png");
        ok &= loadTextureSafe(mapTextures.floors[1], "floortwo.png");
        ok &= loadTextureSafe(mapTextures.floors[2], "floorthree.png");
        ok &= loadTextureSafe(mapTextures.wall, "wall.png");
        ok &= loadTextureSafe(mapTextures.brokenWall, "brokenwall.png");
        ok &= loadTextureSafe(mapTextures.heal, "heal.png");
        ok &= loadTextureSafe(mapTextures.spawn, "spawn.png");
        ok &= loadTextureSafe(mapTextures.barrel, "barrel.png");
        ok &= loadTextureSafe(mapTextures.explodedBarrel, "exploded.png");
        return ok;
    }

    void randomizeFloorTextureIndices()
    {
        for (int i = 0; i < MAP_HEIGHT; i++)
            for (int j = 0; j < MAP_WIDTH; j++)
                floorTextureIndices[i][j] = rand() % 3;
    }
    void initSpawnPointsList()
    {
        spawnPoints.clear();
        for (int i = 0; i < MAP_HEIGHT; i++)
            for (int j = 0; j < MAP_WIDTH; j++)
                if (gameMap[i][j][1] == spawn_point)
                    spawnPoints.push_back({ (float)j * TILE_SIZE + TILE_SIZE / 2.f, (float)i * TILE_SIZE + TILE_SIZE / 2.f });
    }

    enum class CharacterType
    {
        CHEESE_MAN,
        BAZOOKA_MAN,
        DETECTIVE,
        WIZARD,
        ICE_MAN
    };
    namespace CharacterSheetRow
    {
        constexpr int CHEESE_MAN = 0, BAZOOKA_MAN = 1, DETECTIVE = 2, WIZARD = 3, ICE_MAN = 4;
    }
    namespace CharacterFrameSize
    {
        constexpr int W = 32, H = 41;
    }
    constexpr float SPECIAL_RECHARGE = 8.f;
    constexpr float PLAYER_SPRITE_ROT_OFFSET = 270.f;

    struct SpriteAnimator
    {
        int frameCount = 4, frameW = 32, frameH = 41, sheetRow = 0, currentFrame = 0;
        float frameInterval = 0.12f, frameTimer = 0.f;
        bool isWalking = false;
        void setup(int row, int fw, int fh, int frames = 4, float fps = 8.f)
        {
            sheetRow = row;
            frameW = fw;
            frameH = fh;
            frameCount = frames;
            frameInterval = 1.f / fps;
            currentFrame = 0;
            frameTimer = 0.f;
        }
        void update(float dt, bool isMoving)
        {
            isWalking = isMoving;
            if (!isMoving)
            {
                currentFrame = 0;
                frameTimer = 0.f;
                return;
            }
            frameTimer += dt;
            if (frameTimer >= frameInterval)
            {
                frameTimer -= frameInterval;
                currentFrame = (currentFrame + 1) % frameCount;
            }
        }
        sf::IntRect currentRect() const { return sf::IntRect({ currentFrame * frameW, sheetRow * frameH }, { frameW, frameH }); }
        void applyToSprite(std::optional<sf::Sprite>& spr, sf::Texture& tex) const
        {
            sf::Vector2f pos = spr.has_value() ? spr->getPosition() : sf::Vector2f{};
            sf::Angle rot = spr.has_value() ? spr->getRotation() : sf::degrees(0.f);
            sf::Vector2f scl = spr.has_value() ? spr->getScale() : sf::Vector2f{ 1.f, 1.f };
            sf::Color col = spr.has_value() ? spr->getColor() : sf::Color::White;
            spr.emplace(tex, currentRect());
            spr->setOrigin({ frameW / 2.f, frameH / 2.f });
            spr->setPosition(pos);
            spr->setRotation(rot);
            spr->setScale(scl);
            spr->setColor(col);
        }
    };

    enum class EffectType
    {
        NONE,
        STUCK,
        SLOWED,
        CONFUSED
    };
    struct StatusEffect
    {
        EffectType type = EffectType::NONE;
        float duration = 0.f, slowFactor = 0.4f;
        bool active = false;
        void apply(EffectType t, float dur, float slow = 0.4f)
        {
            type = t;
            duration = dur;
            slowFactor = slow;
            active = true;
        }
        void update(float dt)
        {
            if (!active)
                return;
            duration -= dt;
            if (duration <= 0.f)
            {
                type = EffectType::NONE;
                active = false;
            }
        }
        bool isActive() const { return active; }
    };
    enum class BulletOwner
    {
        PLAYER,
        ENEMY
    };
    enum class BulletSpecial
    {
        NORMAL,
        CHEESE,
        BAZOOKA,
        REVOLVER,
        MAGIC,
        ICE
    };

    inline void applyBulletEffect(StatusEffect& target, BulletSpecial special)
    {
        switch (special)
        {
        case BulletSpecial::CHEESE:
            target.apply(EffectType::STUCK, 4.f);
            break;
        case BulletSpecial::MAGIC:
            target.apply(EffectType::CONFUSED, 4.f);
            break;
        case BulletSpecial::ICE:
            target.apply(EffectType::SLOWED, 4.f, 0.4f);
            break;
        default:
            break;
        }
    }

    struct NetBullet
    {
        sf::RectangleShape shape;
        sf::Vector2f velocity;
        float damage = 20.f;
        int senderId = -1, senderTeam = 0, framesAlive = 0;
        bool isVisual = false;
        BulletSpecial special = BulletSpecial::NORMAL;
    };
    struct GunType
    {
        static const int PISTOL = 0, UZI = 1, SHOTGUN = 2, KATANA = 3, BAT = 4;
    };
    struct Gun
    {
        int type;
        float fireRate, bulletSpeed, range;
        bool automatic;
        sf::Clock fireClock;
    };

    struct NetworkPlayer
    {
        std::optional<sf::Sprite> sprite;
        SpriteAnimator animator;
        sf::Texture texture;
        bool textureLoaded = false;
        StatusEffect statusEffect;
        sf::Vector2f position;
        float rotationDeg = 0.f, hp = 100.f, maxHp = 100.f;
        int team = 0;
        CharacterType charType = CharacterType::CHEESE_MAN;
        sf::FloatRect getGlobalBounds() const
        {
            sf::FloatRect b = sprite.has_value() ? sprite->getGlobalBounds() : sf::FloatRect(position - sf::Vector2f{ 16.f, 20.5f }, { 32.f, 41.f });
            b.position.x += 10.f;
            b.position.y += 10.f;
            b.size.x -= 20.f;
            b.size.y -= 20.f;
            return b;
        }
    };

    struct SpecialGun
    {
        float charge = SPECIAL_RECHARGE, maxCharge = SPECIAL_RECHARGE;
        bool isReady() const { return charge >= maxCharge; }
        float barFill() const { return charge / maxCharge; }
        void recharge(float dt)
        {
            if (charge < maxCharge)
                charge += dt;
            if (charge > maxCharge)
                charge = maxCharge;
        }
        bool consume()
        {
            if (!isReady())
                return false;
            charge = 0.f;
            return true;
        }
    };

    struct LocalPlayer
    {
        CharacterType charType = CharacterType::CHEESE_MAN;
        std::string charName = "Cheese Man";
        std::optional<sf::Sprite> sprite;
        SpriteAnimator animator;
        sf::Texture texture;
        sf::Vector2f position, aimDir = { 1.f, 0.f };
        bool isMoving = false;
        StatusEffect statusEffect;
        SpecialGun specialGun;
        NetBullet specialBullets[100];
        void setPosition(sf::Vector2f pos)
        {
            position = pos;
            if (sprite.has_value())
                sprite->setPosition(pos);
        }
        sf::Vector2f getPosition() const { return position; }
        sf::Angle getRotation() const { return sf::degrees(std::atan2(aimDir.y, aimDir.x) * 180.f / 3.14159f + PLAYER_SPRITE_ROT_OFFSET); }
        void move(sf::Vector2f delta)
        {
            position += delta;
            if (sprite.has_value())
                sprite->setPosition(position);
        }
        sf::FloatRect getGlobalBounds() const
        {
            sf::FloatRect b = sprite.has_value() ? sprite->getGlobalBounds() : sf::FloatRect(position - sf::Vector2f{ 16.f, 20.5f }, { 32.f, 41.f });
            b.position.x += 10.f;
            b.position.y += 10.f;
            b.size.x -= 20.f;
            b.size.y -= 20.f;
            return b;
        }

        bool init(CharacterType t, const std::string& texturePath)
        {
            charType = t;
            switch (t)
            {
            case CharacterType::CHEESE_MAN:
                charName = "Cheese Man";
                break;
            case CharacterType::BAZOOKA_MAN:
                charName = "Bazooka Man";
                break;
            case CharacterType::DETECTIVE:
                charName = "The Detective";
                break;
            case CharacterType::WIZARD:
                charName = "The Wizard";
                break;
            case CharacterType::ICE_MAN:
                charName = "Ice Man";
                break;
            }
            if (!texture.loadFromFile(getAssetPath(texturePath).string()))
                return false;
            int row = 0;
            switch (t)
            {
            case CharacterType::CHEESE_MAN:
                row = CharacterSheetRow::CHEESE_MAN;
                break;
            case CharacterType::BAZOOKA_MAN:
                row = CharacterSheetRow::BAZOOKA_MAN;
                break;
            case CharacterType::DETECTIVE:
                row = CharacterSheetRow::DETECTIVE;
                break;
            case CharacterType::WIZARD:
                row = CharacterSheetRow::WIZARD;
                break;
            case CharacterType::ICE_MAN:
                row = CharacterSheetRow::ICE_MAN;
                break;
            }
            animator.setup(row, CharacterFrameSize::W, CharacterFrameSize::H, 4, 8.f);
            animator.applyToSprite(sprite, texture);
            if (sprite.has_value())
                sprite->setScale({ 1.7f, 1.7f });
            specialGun.charge = specialGun.maxCharge;
            return true;
        }

        void update(float dt)
        {
            statusEffect.update(dt);
            specialGun.recharge(dt);
            animator.update(dt, isMoving);
            animator.applyToSprite(sprite, texture);
            if (sprite.has_value())
            {
                sprite->setScale({ 1.7f, 1.7f });
                sprite->setPosition(position);
                sprite->setRotation(getRotation());
            }
        }
        void draw(sf::RenderWindow& window) const
        {
            if (sprite.has_value())
                window.draw(*sprite);
        }
    };

    struct GameState
    {
        LocalPlayer player;
        float playerSpeed = 200.f, playerMaxHp = 100.f, playerHp = 100.f, playerDamage = 15.f;
        int myId = -1, myTeam = 0, redScore = 0, blueScore = 0, winningTeam = 0;
        std::optional<sf::IpAddress> serverIp;
        sf::View camera;
        std::vector<NetBullet> bullets;
        std::map<int, NetworkPlayer> otherPlayers;
        sf::Font font;
        std::vector<BaseStation> bases;
        Gun pistol, uzi, shotgun, katana, bat;
        Gun* currentGun = nullptr;
        bool wasMousePressed = false, wasRightPressed = false;
        bool isCaptureMode = false;
    };

    void initGuns(GameState& state)
    {
        state.pistol = { GunType::PISTOL, 2.0f, 25.f, 0.f, false };
        state.uzi = { GunType::UZI, 6.0f, 20.f, 0.f, true };
        state.shotgun = { GunType::SHOTGUN, 1.0f, 30.f, 0.f, false };
        state.katana = { GunType::KATANA, 2.0f, 0.f, 80.f, false };
        state.bat = { GunType::BAT, 1.5f, 0.f, 100.f, false };
        state.currentGun = &state.pistol;
    }

    void applyCharacterStats(GameState& state)
    {
        switch (state.player.charType)
        {
        case CharacterType::CHEESE_MAN:
            state.playerMaxHp = 100.f;
            state.playerSpeed = 220.f;
            state.playerDamage = 15.f;
            break;
        case CharacterType::BAZOOKA_MAN:
            state.playerMaxHp = 130.f;
            state.playerSpeed = 170.f;
            state.playerDamage = 20.f;
            break;
        case CharacterType::DETECTIVE:
            state.playerMaxHp = 100.f;
            state.playerSpeed = 210.f;
            state.playerDamage = 18.f;
            break;
        case CharacterType::WIZARD:
            state.playerMaxHp = 90.f;
            state.playerSpeed = 200.f;
            state.playerDamage = 12.f;
            break;
        case CharacterType::ICE_MAN:
            state.playerMaxHp = 110.f;
            state.playerSpeed = 195.f;
            state.playerDamage = 14.f;
            break;
        }
        state.playerHp = state.playerMaxHp;
    }

    sf::Vector2f getPlayerStartPos(const GameState* state = nullptr)
    {
        static int lastSpawnIndex = -1;
        if (!spawnPoints.empty())
        {
            int best = -1, tries = 0;
            while (tries < 50)
            {
                int ri = rand() % spawnPoints.size();
                bool ok = (ri != lastSpawnIndex || spawnPoints.size() == 1);
                if (ok && state)
                    for (const auto& pr : state->otherPlayers)
                    {
                        float dx = spawnPoints[ri].x - pr.second.position.x, dy = spawnPoints[ri].y - pr.second.position.y;
                        if (std::sqrt(dx * dx + dy * dy) < TILE_SIZE * 1.5f)
                        {
                            ok = false;
                            break;
                        }
                    }
                if (ok)
                {
                    best = ri;
                    break;
                }
                tries++;
            }
            if (best == -1)
                best = rand() % spawnPoints.size();
            lastSpawnIndex = best;
            return spawnPoints[best];
        }
        for (int i = 1; i < MAP_HEIGHT; i++)
            for (int j = 1; j < MAP_WIDTH; j++)
                if (gameMap[i][j][0] == path_tile)
                    return { (float)j * TILE_SIZE + TILE_SIZE / 2.f, (float)i * TILE_SIZE + TILE_SIZE / 2.f };
        return { 1000.f, 1000.f };
    }

    bool isCollidingWithWall(const sf::FloatRect& bounds)
    {
        for (int i = 0; i < MAP_HEIGHT; i++)
            for (int j = 0; j < MAP_WIDTH; j++)
                if (gameMap[i][j][0] == wall)
                {
                    sf::FloatRect wb({ (float)j * TILE_SIZE, (float)i * TILE_SIZE }, { TILE_SIZE, TILE_SIZE });
                    if (bounds.findIntersection(wb).has_value())
                        return true;
                }
        return false;
    }
    bool isCollidingWithPlayers(const GameState& state)
    {
        sf::FloatRect mb = state.player.getGlobalBounds();
        for (const auto& pr : state.otherPlayers)
            if (mb.findIntersection(pr.second.getGlobalBounds()).has_value())
                return true;
        return false;
    }

    void explodeBarrel(int r, int c, GameState& state, sf::TcpSocket& tcpSocket, bool broadcast = true)
    {
        if (gameMap[r][c][1] != barrel)
            return;
        gameMap[r][c][1] = explodedbarrel;
        if (broadcast)
        {
            sf::Packet pExp;
            pExp << static_cast<int>(PacketType::BarrelExploded) << r << c;
            tcpSocket.send(pExp);
        }
        sf::Vector2f bPos(c * TILE_SIZE + TILE_SIZE / 2.f, r * TILE_SIZE + TILE_SIZE / 2.f);
        sf::Vector2f pPos = state.player.getPosition();
        float dx = pPos.x - bPos.x, dy = pPos.y - bPos.y;
        if (std::sqrt(dx * dx + dy * dy) <= TILE_SIZE * 2.5f)
        {
            state.playerHp -= 50.f;
            if (state.playerHp <= 0.f)
            {
                sf::Packet dp;
                dp << static_cast<int>(PacketType::PlayerDied) << state.myTeam << state.myTeam;
                tcpSocket.send(dp);
                state.player.setPosition(getPlayerStartPos(&state));
                state.playerHp = state.playerMaxHp;
            }
        }
        for (int dr = -2; dr <= 2; dr++)
            for (int dc = -2; dc <= 2; dc++)
            {
                int nr = r + dr, nc = c + dc;
                if (nr >= 0 && nr < MAP_HEIGHT && nc >= 0 && nc < MAP_WIDTH)
                {
                    if (gameMap[nr][nc][0] == wall && nr != 0 && nc != 0 && nr != MAP_HEIGHT - 1 && nc != MAP_WIDTH - 1)
                        gameMap[nr][nc][0] = brokenwall;
                    if (gameMap[nr][nc][1] == barrel)
                        explodeBarrel(nr, nc, state, tcpSocket, broadcast);
                }
            }
    }

    void initMapEntitiesWithBases(GameState& state)
    {
        initSpawnPointsList();
        state.bases.clear();
        for (int i = 0; i < MAP_HEIGHT; i++)
        {
            for (int j = 0; j < MAP_WIDTH; j++)
            {
                if (gameMap[i][j][1] == base_station)
                {
                    BaseStation b;
                    b.shape.setSize(sf::Vector2f(TILE_SIZE * 1.5f, TILE_SIZE * 1.5f));
                    b.shape.setFillColor(sf::Color(100, 100, 100, 150));
                    b.shape.setOrigin({ (TILE_SIZE * 1.5f) / 2.f, (TILE_SIZE * 1.5f) / 2.f });
                    b.shape.setPosition({ (float)j * TILE_SIZE + TILE_SIZE / 2.f, (float)i * TILE_SIZE + TILE_SIZE / 2.f });
                    state.bases.push_back(b);
                }
            }
        }
    }

    bool setupNetwork(sf::TcpSocket& tcpSocket, sf::UdpSocket& udpSocket, GameState& state)
    {
        cout << "Connecting to server at " << SERVER_IP << "..." << endl;
        auto serverIpOpt = sf::IpAddress::resolve(SERVER_IP);
        if (!serverIpOpt.has_value())
        {
            cout << "Invalid IP Address!" << endl;
            return false;
        }
        state.serverIp = serverIpOpt;

        // Timeout to avoid infinite Not Responding loop
        if (tcpSocket.connect(state.serverIp.value(), SERVER_PORT, sf::seconds(2.f)) != sf::Socket::Status::Done)
        {
            cout << "Connection failed! Server not running." << endl;
            return false;
        }

        sf::Packet initPacket;
        if (tcpSocket.receive(initPacket) == sf::Socket::Status::Done)
        {
            int type;
            initPacket >> type >> state.myId >> state.redScore >> state.blueScore;
            for (int i = 0; i < MAP_HEIGHT; i++)
                for (int j = 0; j < MAP_WIDTH; j++)
                    initPacket >> gameMap[i][j][0] >> gameMap[i][j][1];
            if (state.isCaptureMode)
                initMapEntitiesWithBases(state);
            else
                initSpawnPointsList();
            randomizeFloorTextureIndices();
        }
        else
        {
            cout << "Failed to receive map data!" << endl;
            return false;
        }

        tcpSocket.setBlocking(false);
        udpSocket.bind(sf::Socket::AnyPort);
        udpSocket.setBlocking(false);
        sf::Packet handshake;
        handshake << static_cast<int>(PacketType::UdpHandshake) << state.myId;
        udpSocket.send(handshake, state.serverIp.value(), SERVER_PORT);
        return true;
    }

    void selectCharacterPhase(sf::RenderWindow& window, GameState& state)
    {
        constexpr int COUNT = 5;
        const float btnW = 270.f, btnH = 360.f, gap = 25.f;
        const float totalW = COUNT * btnW + (COUNT - 1) * gap;
        const float startX = (VIRTUAL_SCREEN_WIDTH - totalW) / 2.f, startY = 200.f;
        const char* names[] = { "Cheese Man", "Bazooka Man", "The Detective", "The Wizard", "Ice Man" };
        const char* descs[] = { "[RMB] Cheese Gun  — Sticks enemy", "[RMB] Bazooka     — Explosion", "[RMB] Revolver    — Pierces walls", "[RMB] Magic Bolt  — Confuses enemy", "[RMB] Ice Shard   — Slows enemy" };
        bool chosen = false;
        while (window.isOpen() && !chosen)
        {
            while (const std::optional<sf::Event> ev = window.pollEvent())
            {
                if (ev->is<sf::Event::Closed>())
                {
                    window.close();
                    return;
                }
                if (const auto* mb = ev->getIf<sf::Event::MouseButtonPressed>())
                {
                    if (mb->button == sf::Mouse::Button::Left)
                    {
                        sf::Vector2f mp = mapPixelToVirtualScreen(window, mb->position);
                        for (int i = 0; i < COUNT; i++)
                        {
                            sf::FloatRect btn({ startX + i * (btnW + gap), startY }, { btnW, btnH });
                            if (btn.contains(mp))
                            {
                                state.player.init(static_cast<CharacterType>(i), "characters.png");
                                applyCharacterStats(state);
                                chosen = true;
                                break;
                            }
                        }
                    }
                }
            }
            window.clear(sf::Color(20, 20, 30));
            window.setView(makeVirtualScreenView(window));
            sf::Text title(state.font, "Choose Your Character", 52);
            title.setFillColor(sf::Color::White);
            title.setPosition({ startX, 90.f });
            window.draw(title);
            for (int i = 0; i < COUNT; i++)
            {
                float bx = startX + i * (btnW + gap);
                sf::RectangleShape card({ btnW, btnH });
                card.setPosition({ bx, startY });
                card.setFillColor(sf::Color(45, 45, 75));
                card.setOutlineColor(sf::Color(100, 100, 200));
                card.setOutlineThickness(2.f);
                window.draw(card);
                sf::Text nameT(state.font, names[i], 28);
                nameT.setFillColor(sf::Color::Cyan);
                nameT.setPosition({ bx + 10.f, startY + 10.f });
                window.draw(nameT);
                sf::Text descT(state.font, descs[i], 19);
                descT.setFillColor(sf::Color(220, 220, 120));
                descT.setPosition({ bx + 10.f, startY + 60.f });
                window.draw(descT);
            }
            window.display();
        }
    }

    void selectTeamPhase(sf::RenderWindow& window, GameState& state)
    {
        sf::RectangleShape redBtn({ 400.f, 400.f }), blueBtn({ 400.f, 400.f });
        redBtn.setFillColor(sf::Color(200, 50, 50));
        redBtn.setPosition({ 300.f, 300.f });
        blueBtn.setFillColor(sf::Color(50, 50, 200));
        blueBtn.setPosition({ 1000.f, 300.f });
        sf::Text txt(state.font, "Choose your team", 40);
        txt.setFillColor(sf::Color::White);
        txt.setPosition({ 500.f, 100.f });
        while (window.isOpen() && state.myTeam == 0)
        {
            while (const std::optional<sf::Event> ev = window.pollEvent())
            {
                if (ev->is<sf::Event::Closed>())
                    window.close();
                if (const auto* mb = ev->getIf<sf::Event::MouseButtonPressed>())
                {
                    if (mb->button == sf::Mouse::Button::Left)
                    {
                        sf::Vector2f mp = mapPixelToVirtualScreen(window, mb->position);
                        if (redBtn.getGlobalBounds().contains(mp))
                            state.myTeam = 1;
                        if (blueBtn.getGlobalBounds().contains(mp))
                            state.myTeam = 2;
                    }
                }
            }
            window.clear(sf::Color(30, 30, 30));
            window.setView(makeVirtualScreenView(window));
            window.draw(redBtn);
            window.draw(blueBtn);
            window.draw(txt);
            window.display();
        }
        if (state.player.sprite.has_value())
            state.player.sprite->setColor(state.myTeam == 1 ? sf::Color(255, 100, 100) : sf::Color(100, 100, 255));
    }

    void shootNormal(GameState& state, sf::TcpSocket& tcpSocket)
    {
        float delay = 1.f / state.currentGun->fireRate;
        if (state.currentGun->fireClock.getElapsedTime().asSeconds() < delay)
            return;
        int type = state.currentGun->type;
        float bSpeed = state.currentGun->bulletSpeed;
        float finalDmg = state.playerDamage;
        if (type == GunType::UZI)
            finalDmg *= 0.3f;
        else if (type == GunType::SHOTGUN)
            finalDmg *= 1.5f;
        else if (type == GunType::KATANA)
            finalDmg *= 2.5f;
        else if (type == GunType::BAT)
            finalDmg *= 2.0f;
        sf::Vector2f pPos = state.player.getPosition();
        sf::Vector2f aimDir = state.player.aimDir;
        float aimAngleDeg = std::atan2(aimDir.y, aimDir.x) * 180.f / 3.14159f;

        auto makeNetBullet = [&](float angleDeg, float dmg) -> NetBullet
            {
                NetBullet b;
                b.shape.setSize({ 15.f, 5.f });
                b.shape.setOrigin({ 7.5f, 2.5f });
                b.shape.setPosition(pPos);
                b.shape.setRotation(sf::degrees(angleDeg));
                float rad = angleDeg * 3.14159f / 180.f;
                b.velocity = { std::cos(rad) * bSpeed, std::sin(rad) * bSpeed };
                b.damage = dmg;
                b.senderId = state.myId;
                b.senderTeam = state.myTeam;
                b.special = BulletSpecial::NORMAL;
                return b;
            };

        if (type == GunType::PISTOL || type == GunType::UZI)
        {
            NetBullet b = makeNetBullet(aimAngleDeg, finalDmg);
            b.shape.setFillColor(type == GunType::PISTOL ? sf::Color::Yellow : sf::Color(255, 165, 0));
            state.bullets.push_back(b);
            sf::Packet p;
            p << static_cast<int>(PacketType::BulletFired) << pPos.x << pPos.y << aimAngleDeg << finalDmg << state.myTeam << static_cast<int>(BulletSpecial::NORMAL);
            tcpSocket.send(p);
        }
        else if (type == GunType::SHOTGUN)
        {
            for (float spread : {-15.f, 0.f, 15.f})
            {
                NetBullet b = makeNetBullet(aimAngleDeg + spread, finalDmg);
                b.shape.setSize({ 10.f, 5.f });
                b.shape.setFillColor(sf::Color::Red);
                b.shape.setOrigin({ 5.f, 2.5f });
                state.bullets.push_back(b);
                sf::Packet p;
                p << static_cast<int>(PacketType::BulletFired) << pPos.x << pPos.y << aimAngleDeg + spread << finalDmg << state.myTeam << static_cast<int>(BulletSpecial::NORMAL);
                tcpSocket.send(p);
            }
        }
        else if (type == GunType::KATANA || type == GunType::BAT)
        {
            NetBullet slash;
            slash.shape.setSize({ 20.f, 40.f });
            slash.shape.setFillColor(type == GunType::KATANA ? sf::Color::White : sf::Color(139, 69, 19));
            slash.shape.setOrigin({ 10.f, 20.f });
            slash.shape.setPosition(pPos + aimDir * 30.f);
            slash.shape.setRotation(sf::degrees(aimAngleDeg));
            slash.velocity = aimDir * 5.f;
            slash.damage = 0;
            slash.senderId = state.myId;
            slash.isVisual = true;
            state.bullets.push_back(slash);
            for (const auto& pr : state.otherPlayers)
            {
                sf::Vector2f toE = pr.second.position - pPos;
                float dist = std::sqrt(toE.x * toE.x + toE.y * toE.y);
                if (dist > state.currentGun->range)
                    continue;
                float dot = aimDir.x * (toE.x / dist) + aimDir.y * (toE.y / dist);
                if (dot > 0.5f)
                {
                    sf::Packet p;
                    p << static_cast<int>(PacketType::BulletFired) << pr.second.position.x << pr.second.position.y << 0.f << finalDmg << state.myTeam << static_cast<int>(BulletSpecial::NORMAL);
                    tcpSocket.send(p);
                }
            }
        }
        state.currentGun->fireClock.restart();
    }

    void shootSpecial(GameState& state, sf::TcpSocket& tcpSocket)
    {
        if (!state.player.specialGun.consume())
            return;
        BulletSpecial bs = BulletSpecial::NORMAL;
        float dmg = 5.f, spd = 15.f;
        switch (state.player.charType)
        {
        case CharacterType::CHEESE_MAN:
            bs = BulletSpecial::CHEESE;
            dmg = 5.f;
            break;
        case CharacterType::BAZOOKA_MAN:
            bs = BulletSpecial::BAZOOKA;
            dmg = 80.f;
            break;
        case CharacterType::DETECTIVE:
            bs = BulletSpecial::REVOLVER;
            dmg = 40.f;
            break;
        case CharacterType::WIZARD:
            bs = BulletSpecial::MAGIC;
            dmg = 5.f;
            break;
        case CharacterType::ICE_MAN:
            bs = BulletSpecial::ICE;
            dmg = 5.f;
            break;
        }
        sf::Vector2f pos = state.player.getPosition();
        float rot = std::atan2(state.player.aimDir.y, state.player.aimDir.x) * 180.f / 3.14159f;

        NetBullet nb;
        nb.special = bs;
        nb.shape.setSize({ 15.f, 5.f });
        nb.shape.setOrigin({ 7.5f, 2.5f });
        if (nb.special == BulletSpecial::CHEESE)
            nb.shape.setFillColor(sf::Color(255, 220, 0));
        else if (nb.special == BulletSpecial::BAZOOKA)
        {
            nb.shape.setFillColor(sf::Color(255, 100, 0));
            nb.shape.setScale({ 1.5f, 1.5f });
        }
        else if (nb.special == BulletSpecial::REVOLVER)
            nb.shape.setFillColor(sf::Color(180, 180, 180));
        else if (nb.special == BulletSpecial::MAGIC)
            nb.shape.setFillColor(sf::Color(180, 0, 255));
        else if (nb.special == BulletSpecial::ICE)
            nb.shape.setFillColor(sf::Color(100, 200, 255));
        else
            nb.shape.setFillColor(sf::Color::White);

        nb.damage = dmg;
        nb.shape.setPosition({ pos.x, pos.y });
        nb.shape.setRotation(sf::degrees(rot));
        nb.senderId = state.myId;
        nb.senderTeam = state.myTeam;
        float rad = rot * 3.14159f / 180.f;
        nb.velocity = { std::cos(rad) * spd, std::sin(rad) * spd };
        state.bullets.push_back(nb);
        sf::Packet p;
        p << static_cast<int>(PacketType::BulletFired) << pos.x << pos.y << rot << dmg << state.myTeam << static_cast<int>(bs);
        tcpSocket.send(p);
    }

    void processNetworkEvents(sf::TcpSocket& tcpSocket, sf::UdpSocket& udpSocket, GameState& state)
    {
        sf::Packet tcpPacket;
        while (tcpSocket.receive(tcpPacket) == sf::Socket::Status::Done)
        {
            int typeInt;
            tcpPacket >> typeInt;
            PacketType type = static_cast<PacketType>(typeInt);

            // Client-Side Prediction for Player Deaths
            if (type == PacketType::PlayerDied)
            {
                int killerTeam, deadTeam;
                tcpPacket >> killerTeam >> deadTeam;
                if (killerTeam == 1)
                    state.redScore++;
                else if (killerTeam == 2)
                    state.blueScore++;
            }
            else if (type == PacketType::ScoreUpdate)
            {
                int rS, bS;
                tcpPacket >> rS >> bS;
                if (rS > state.redScore)
                    state.redScore = rS;
                if (bS > state.blueScore)
                    state.blueScore = bS;
            }
            else if (type == PacketType::GameOver)
                tcpPacket >> state.winningTeam;
            else if (type == PacketType::BarrelExploded)
            {
                int r, c;
                tcpPacket >> r >> c;
                if (gameMap[r][c][1] == barrel)
                    explodeBarrel(r, c, state, tcpSocket, false);
            }
            else if (type == PacketType::BaseCaptured && state.isCaptureMode)
            {
                int baseIndex, teamId;
                tcpPacket >> baseIndex >> teamId;
                if (!state.bases[baseIndex].isCaptured)
                {
                    state.bases[baseIndex].isCaptured = true;
                    state.bases[baseIndex].shape.setFillColor(teamId == 1 ? sf::Color(200, 0, 0, 150) : sf::Color(0, 0, 200, 150));
                    if (teamId == 1)
                        state.redScore++;
                    else if (teamId == 2)
                        state.blueScore++;
                }
            }
            else
            {
                int senderId;
                tcpPacket >> senderId;
                if (type == PacketType::BulletFired)
                {
                    float x, y, rot, dmg;
                    int bulletTeam, specialInt;
                    tcpPacket >> x >> y >> rot >> dmg >> bulletTeam >> specialInt;
                    NetBullet nb;
                    nb.special = static_cast<BulletSpecial>(specialInt);
                    nb.shape.setSize({ 15.f, 5.f });
                    nb.shape.setOrigin({ 7.5f, 2.5f });
                    if (nb.special == BulletSpecial::CHEESE)
                        nb.shape.setFillColor(sf::Color(255, 220, 0));
                    else if (nb.special == BulletSpecial::BAZOOKA)
                    {
                        nb.shape.setFillColor(sf::Color(255, 100, 0));
                        nb.shape.setScale({ 1.5f, 1.5f });
                    }
                    else if (nb.special == BulletSpecial::REVOLVER)
                        nb.shape.setFillColor(sf::Color(180, 180, 180));
                    else if (nb.special == BulletSpecial::MAGIC)
                        nb.shape.setFillColor(sf::Color(180, 0, 255));
                    else if (nb.special == BulletSpecial::ICE)
                        nb.shape.setFillColor(sf::Color(100, 200, 255));
                    else
                        nb.shape.setFillColor(sf::Color::White);
                    nb.damage = dmg;
                    nb.shape.setPosition({ x, y });
                    nb.shape.setRotation(sf::degrees(rot));
                    nb.senderId = senderId;
                    nb.senderTeam = bulletTeam;
                    float rad = rot * 3.14159f / 180.f;
                    nb.velocity = { std::cos(rad) * 15.f, std::sin(rad) * 15.f };
                    state.bullets.push_back(nb);
                }
                else if (type == PacketType::Disconnect)
                    state.otherPlayers.erase(senderId);
            }
        }
        sf::Packet udpPacket;
        std::optional<sf::IpAddress> senderIp;
        unsigned short senderPort;
        while (udpSocket.receive(udpPacket, senderIp, senderPort) == sf::Socket::Status::Done)
        {
            int typeInt;
            udpPacket >> typeInt;
            PacketType type = static_cast<PacketType>(typeInt);
            if (type == PacketType::PlayerUpdate)
            {
                int senderId;
                float x, y, rot, hp;
                int team, charTypeInt;
                udpPacket >> senderId >> x >> y >> rot >> hp >> team >> charTypeInt;
                auto& np = state.otherPlayers[senderId];
                np.charType = static_cast<CharacterType>(charTypeInt);
                if (!np.textureLoaded)
                {
                    if (loadTextureSafe(np.texture, "characters.png"))
                    {
                        int row = 0;
                        switch (np.charType)
                        {
                        case CharacterType::CHEESE_MAN:
                            row = CharacterSheetRow::CHEESE_MAN;
                            break;
                        case CharacterType::BAZOOKA_MAN:
                            row = CharacterSheetRow::BAZOOKA_MAN;
                            break;
                        case CharacterType::DETECTIVE:
                            row = CharacterSheetRow::DETECTIVE;
                            break;
                        case CharacterType::WIZARD:
                            row = CharacterSheetRow::WIZARD;
                            break;
                        case CharacterType::ICE_MAN:
                            row = CharacterSheetRow::ICE_MAN;
                            break;
                        }
                        np.animator.setup(row, CharacterFrameSize::W, CharacterFrameSize::H, 4, 8.f);
                        np.animator.applyToSprite(np.sprite, np.texture);
                        if (np.sprite.has_value())
                            np.sprite->setScale({ 1.7f, 1.7f });
                        np.textureLoaded = true;
                    }
                }
                np.position = { x, y };
                np.rotationDeg = rot;
                np.hp = hp;
                np.team = team;
                if (hp > np.maxHp)
                    np.maxHp = hp;
                if (np.sprite.has_value())
                {
                    np.sprite->setPosition({ x, y });
                    np.sprite->setRotation(sf::degrees(rot));
                    np.sprite->setColor(team == 1 ? sf::Color(255, 100, 100) : sf::Color(100, 100, 255));
                }
            }
        }
    }

    void handlePickupsAndBases(GameState& state, sf::TcpSocket& tcpSocket)
    {
        int pCol = static_cast<int>(state.player.getPosition().x / TILE_SIZE), pRow = static_cast<int>(state.player.getPosition().y / TILE_SIZE);
        if (pRow >= 0 && pRow < MAP_HEIGHT && pCol >= 0 && pCol < MAP_WIDTH)
        {
            int ent = gameMap[pRow][pCol][1];
            if (ent == heal && state.playerHp < state.playerMaxHp)
            {
                state.playerHp = std::min(state.playerHp + 25.f, state.playerMaxHp);
                gameMap[pRow][pCol][1] = empty_entity;
            }
            else if (ent == powerup_maxhp)
            {
                state.playerMaxHp += 50.f;
                state.playerHp = state.playerMaxHp;
                gameMap[pRow][pCol][1] = empty_entity;
            }
            else if (ent == powerup_speed)
            {
                state.playerSpeed += 2.0f;
                gameMap[pRow][pCol][1] = empty_entity;
            }
            else if (ent == powerup_damage)
            {
                state.playerDamage += 15.f;
                gameMap[pRow][pCol][1] = empty_entity;
            }
        }

        // Fast Capture Logic
        if (state.isCaptureMode)
        {
            for (int i = 0; i < state.bases.size(); i++)
            {
                if (!state.bases[i].isCaptured)
                {
                    if (state.player.getGlobalBounds().findIntersection(state.bases[i].shape.getGlobalBounds()).has_value())
                    {
                        state.bases[i].captureProgress += 0.5f;
                        if (state.bases[i].captureProgress >= 100.f)
                        {
                            state.bases[i].isCaptured = true;
                            state.bases[i].captureProgress = 100.f;
                            state.bases[i].shape.setFillColor(state.myTeam == 1 ? sf::Color(200, 0, 0, 150) : sf::Color(0, 0, 200, 150));
                            if (state.myTeam == 1)
                                state.redScore++;
                            else if (state.myTeam == 2)
                                state.blueScore++;
                            sf::Packet p;
                            p << static_cast<int>(PacketType::BaseCaptured) << i << state.myTeam;
                            tcpSocket.send(p);
                        }
                    }
                    else
                    {
                        if (state.bases[i].captureProgress > 0.f)
                        {
                            state.bases[i].captureProgress = std::max(0.f, state.bases[i].captureProgress - 0.2f);
                        }
                    }
                }
            }
        }
    }

    void updatePlayerState(GameState& state, sf::RenderWindow& window, float dt)
    {
        applyLetterboxViewport(state.camera, window.getSize());
        sf::Vector2f dir(0.f, 0.f);
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W))
            dir.y -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S))
            dir.y += 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A))
            dir.x -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D))
            dir.x += 1.f;
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 0.f)
            dir /= len;
        state.player.isMoving = (len > 0.f);
        if (state.player.statusEffect.type == EffectType::CONFUSED)
            dir.x = -dir.x;
        float spd = state.playerSpeed;
        if (state.player.statusEffect.type == EffectType::STUCK)
            spd = 0.f;
        else if (state.player.statusEffect.type == EffectType::SLOWED)
            spd *= state.player.statusEffect.slowFactor;
        if (spd == 0.f)
            state.player.isMoving = false;
        sf::Vector2f move = dir * spd * dt;
        state.player.move({ move.x, 0.f });
        if (isCollidingWithWall(state.player.getGlobalBounds()) || isCollidingWithPlayers(state))
            state.player.move({ -move.x, 0.f });
        state.player.move({ 0.f, move.y });
        if (isCollidingWithWall(state.player.getGlobalBounds()) || isCollidingWithPlayers(state))
            state.player.move({ 0.f, -move.y });
        sf::Vector2f mouseWorld = window.mapPixelToCoords(sf::Mouse::getPosition(window), state.camera);
        sf::Vector2f toMouse = mouseWorld - state.player.getPosition();
        float mLen = std::sqrt(toMouse.x * toMouse.x + toMouse.y * toMouse.y);
        if (mLen > 0.f)
            state.player.aimDir = toMouse / mLen;
        state.player.update(dt);
    }

    void handleNetBulletCollisions(GameState& state, sf::TcpSocket& tcpSocket)
    {
        for (int i = 0; i < (int)state.bullets.size();)
        {
            auto& b = state.bullets[i];
            b.shape.move(b.velocity);
            b.framesAlive++;
            bool destroy = false;
            if (b.isVisual)
            {
                if (b.framesAlive > 5)
                    destroy = true;
            }
            else
            {
                int bc = static_cast<int>(b.shape.getPosition().x / TILE_SIZE), br = static_cast<int>(b.shape.getPosition().y / TILE_SIZE);
                if (isCollidingWithWall(b.shape.getGlobalBounds()) || bc < 0 || bc >= MAP_WIDTH || br < 0 || br >= MAP_HEIGHT)
                    destroy = true;
                else if (gameMap[br][bc][1] == barrel)
                {
                    explodeBarrel(br, bc, state, tcpSocket, true);
                    destroy = true;
                }
                if (!destroy && b.senderId != state.myId)
                {
                    if (b.shape.getGlobalBounds().findIntersection(state.player.getGlobalBounds()).has_value())
                    {
                        state.playerHp -= b.damage;
                        applyBulletEffect(state.player.statusEffect, b.special);
                        destroy = true;
                        if (state.playerHp <= 0.f)
                        {
                            if (b.senderTeam == 1)
                                state.redScore++;
                            else if (b.senderTeam == 2)
                                state.blueScore++;
                            sf::Packet dp;
                            dp << static_cast<int>(PacketType::PlayerDied) << b.senderTeam << state.myTeam;
                            tcpSocket.send(dp);
                            state.player.setPosition(getPlayerStartPos(&state));
                            state.playerHp = state.playerMaxHp;
                        }
                    }
                }
                if (!destroy && b.senderId == state.myId)
                {
                    for (const auto& pr : state.otherPlayers)
                    {
                        if (b.shape.getGlobalBounds().findIntersection(pr.second.getGlobalBounds()).has_value())
                        {
                            destroy = true;
                            break;
                        }
                    }
                }
            }
            if (destroy)
                state.bullets.erase(state.bullets.begin() + i);
            else
                i++;
        }
    }

    void renderFrame(sf::RenderWindow& window, GameState& state)
    {
        window.clear(sf::Color(30, 30, 30));
        applyLetterboxViewport(state.camera, window.getSize());
        window.setView(state.camera);

        // 1. Draw Map (Floor and Walls) first
        for (int i = 0; i < MAP_HEIGHT; i++)
        {
            for (int j = 0; j < MAP_WIDTH; j++)
            {
                sf::Vector2f tilePos{ (float)j * TILE_SIZE, (float)i * TILE_SIZE };
                int floorIndex = floorTextureIndices[i][j] % 3;
                sf::Sprite fs(mapTextures.floors[floorIndex]);
                fs.setPosition(tilePos);
                fs.setScale({ TILE_SIZE / fs.getLocalBounds().size.x, TILE_SIZE / fs.getLocalBounds().size.y });
                window.draw(fs);
                if (gameMap[i][j][0] == wall || gameMap[i][j][0] == brokenwall)
                {
                    sf::Sprite ws(gameMap[i][j][0] == wall ? mapTextures.wall : mapTextures.brokenWall);
                    ws.setPosition(tilePos);
                    ws.setScale({ TILE_SIZE / ws.getLocalBounds().size.x, TILE_SIZE / ws.getLocalBounds().size.y });
                    window.draw(ws);
                }
                int ent = gameMap[i][j][1];
                if (ent == spawn_point)
                {
                    sf::Sprite sp(mapTextures.spawn);
                    sp.setPosition(tilePos);
                    sp.setScale({ TILE_SIZE / sp.getLocalBounds().size.x, TILE_SIZE / sp.getLocalBounds().size.y });
                    window.draw(sp);
                }
                else if (ent == barrel || ent == explodedbarrel)
                {
                    sf::Sprite bs(ent == barrel ? mapTextures.barrel : mapTextures.explodedBarrel);
                    bs.setPosition(tilePos);
                    bs.setScale({ TILE_SIZE / bs.getLocalBounds().size.x, TILE_SIZE / bs.getLocalBounds().size.y });
                    window.draw(bs);
                }
                else if (ent == heal)
                {
                    sf::Sprite hs(mapTextures.heal);
                    hs.setPosition(tilePos);
                    hs.setScale({ TILE_SIZE / hs.getLocalBounds().size.x, TILE_SIZE / hs.getLocalBounds().size.y });
                    window.draw(hs);
                }
                else if (ent >= powerup_maxhp && ent <= powerup_damage)
                {
                    sf::RectangleShape p({ TILE_SIZE / 2.f, TILE_SIZE / 2.f });
                    p.setPosition({ tilePos.x + TILE_SIZE / 4.f, tilePos.y + TILE_SIZE / 4.f });
                    if (ent == powerup_maxhp)
                        p.setFillColor(sf::Color::Magenta);
                    else if (ent == powerup_speed)
                        p.setFillColor(sf::Color::Yellow);
                    else if (ent == powerup_damage)
                        p.setFillColor(sf::Color(255, 128, 0));
                    window.draw(p);
                }
            }
        }

        // 2. Draw bases above floor (Fix for invisible capture bar)
        if (state.isCaptureMode)
        {
            for (const auto& base : state.bases)
            {
                window.draw(base.shape);
                if (!base.isCaptured)
                {
                    sf::RectangleShape progBase(sf::Vector2f(60.f, 6.f));
                    progBase.setFillColor(sf::Color::Red);
                    progBase.setPosition({ base.shape.getPosition().x - 30.f, base.shape.getPosition().y - 50.f });
                    sf::RectangleShape progBar(sf::Vector2f(60.f * (base.captureProgress / 100.f), 6.f));
                    progBar.setFillColor(sf::Color::Blue);
                    progBar.setPosition(progBase.getPosition());
                    window.draw(progBase);
                    window.draw(progBar);
                }
            }
        }

        // 3. Draw Players and Entities
        for (auto& pr : state.otherPlayers)
        {
            NetworkPlayer& np = pr.second;
            if (np.sprite.has_value())
                window.draw(*np.sprite);
            sf::RectangleShape hbg({ 50.f, 6.f });
            hbg.setFillColor(sf::Color(60, 60, 60));
            hbg.setPosition({ np.position.x - 25.f, np.position.y - 50.f });
            sf::RectangleShape hfg({ 50.f * (np.hp / np.maxHp), 6.f });
            hfg.setFillColor(sf::Color::Green);
            hfg.setPosition(hbg.getPosition());
            window.draw(hbg);
            window.draw(hfg);
        }
        for (const auto& b : state.bullets)
            window.draw(b.shape);
        state.player.draw(window);
        sf::Vector2f pPos = state.player.getPosition();
        sf::RectangleShape pbg({ 50.f, 6.f });
        pbg.setFillColor(sf::Color(60, 60, 60));
        pbg.setPosition({ pPos.x - 25.f, pPos.y - 50.f });
        sf::RectangleShape pfg({ 50.f * (state.playerHp / state.playerMaxHp), 6.f });
        pfg.setFillColor(sf::Color::Cyan);
        pfg.setPosition(pbg.getPosition());
        window.draw(pbg);
        window.draw(pfg);
        window.setView(makeVirtualScreenView(window));
        sf::Text scoreT(state.font, (state.isCaptureMode ? "RED BASES: " : "RED SCORE: ") + to_string(state.redScore) + (state.isCaptureMode ? "   |   BLUE BASES: " : "   |   BLUE SCORE: ") + to_string(state.blueScore), 40);
        scoreT.setPosition({ VIRTUAL_SCREEN_WIDTH / 2.f - 200.f, 20.f });
        scoreT.setFillColor(sf::Color::White);
        window.draw(scoreT);
        if (state.winningTeam != 0)
        {
            sf::RectangleShape overlay({ VIRTUAL_SCREEN_WIDTH, VIRTUAL_SCREEN_HEIGHT });
            overlay.setFillColor(sf::Color(0, 0, 0, 180));
            window.draw(overlay);
            string winMsg = (state.winningTeam == 1) ? "RED TEAM WINS!" : "BLUE TEAM WINS!";
            sf::Text winText(state.font, winMsg, 120);
            winText.setFillColor(state.winningTeam == 1 ? sf::Color::Red : sf::Color::Blue);
            sf::FloatRect textRect = winText.getLocalBounds();
            winText.setOrigin({ textRect.position.x + textRect.size.x / 2.0f, textRect.position.y + textRect.size.y / 2.0f });
            winText.setPosition({ VIRTUAL_SCREEN_WIDTH / 2.0f, VIRTUAL_SCREEN_HEIGHT / 2.0f });
            window.draw(winText);
        }
        window.display();
    }

    void runOnlineGame(sf::RenderWindow& window, int& menucounter, bool isCapture)
    {
        GameState state;
        state.isCaptureMode = isCapture;
        initGuns(state);
        if (!loadMapTextures())
            cout << "Warning: Map textures missing." << endl;
        if (!openFontSafe(state.font, "Star_Crush.ttf") && !openFontSafe(state.font, "arial.ttf"))
            cout << "Warning: Font missing." << endl;
        sf::TcpSocket tcpSocket;
        sf::UdpSocket udpSocket;
        if (!setupNetwork(tcpSocket, udpSocket, state))
        {
            menucounter = 16;
            return;
        }
        state.camera.setSize({ 1280.f, 720.f });
        selectCharacterPhase(window, state);
        selectTeamPhase(window, state);
        state.player.setPosition(getPlayerStartPos(&state));
        sf::Clock frameClock;
        while (window.isOpen())
        {
            float dt = frameClock.restart().asSeconds();
            processNetworkEvents(tcpSocket, udpSocket, state);
            state.camera.setCenter(state.player.getPosition());
            if (state.winningTeam == 0)
            {
                while (const std::optional<sf::Event> ev = window.pollEvent())
                {
                    if (ev->is<sf::Event::Closed>())
                    {
                        window.close();
                        return;
                    }
                    if (const auto* ke = ev->getIf<sf::Event::KeyPressed>())
                    {
                        if (ke->code == sf::Keyboard::Key::Escape)
                        {
                            menucounter = 16;
                            return;
                        }
                        if (ke->code == sf::Keyboard::Key::Num1)
                            state.currentGun = &state.pistol;
                        if (ke->code == sf::Keyboard::Key::Num2)
                            state.currentGun = &state.uzi;
                        if (ke->code == sf::Keyboard::Key::Num3)
                            state.currentGun = &state.shotgun;
                        if (ke->code == sf::Keyboard::Key::Num4)
                            state.currentGun = &state.katana;
                        if (ke->code == sf::Keyboard::Key::Num5)
                            state.currentGun = &state.bat;
                    }
                }
                bool rightNow = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
                if (rightNow && !state.wasRightPressed)
                    shootSpecial(state, tcpSocket);
                state.wasRightPressed = rightNow;
                bool leftNow = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
                if (leftNow && (state.currentGun->automatic || !state.wasMousePressed))
                    shootNormal(state, tcpSocket);
                state.wasMousePressed = leftNow;
                updatePlayerState(state, window, dt);
                handlePickupsAndBases(state, tcpSocket);
                handleNetBulletCollisions(state, tcpSocket);
                static sf::Clock networkClock;
                if (networkClock.getElapsedTime().asSeconds() > 0.03f)
                {
                    sf::Packet myUpdate;
                    myUpdate << static_cast<int>(PacketType::PlayerUpdate) << state.myId << state.player.getPosition().x << state.player.getPosition().y << state.player.getRotation().asDegrees() << state.playerHp << state.myTeam << static_cast<int>(state.player.charType);
                    if (state.serverIp.has_value())
                        udpSocket.send(myUpdate, state.serverIp.value(), SERVER_PORT);
                    networkClock.restart();
                }
            }
            renderFrame(window, state);
        }
    }
} // end OnlineMode

// ====================================================================================
// ================================== MAIN MENU =======================================
// ====================================================================================
void setupMenu(std::vector<sf::Text>& menu, const std::vector<sf::String>& strings, sf::Font* font)
{
    menu.clear();
    for (size_t i = 0; i < strings.size(); i++)
    {
        menu.emplace_back(*font);
        menu[i].setString(strings[i]);
        menu[i].setCharacterSize(60);
        menu[i].setPosition({ 270.f, 240.f + (float)i * 120.f });
        menu[i].setFillColor(sf::Color::White);
    }
}

int getClickedItem(std::vector<sf::Text>& menu, Vector2f mousePos)
{
    for (size_t i = 0; i < menu.size(); i++)
    {
        if (menu[i].getGlobalBounds().contains(mousePos))
            return static_cast<int>(i);
    }
    return -1;
}

void draw_menu(RenderWindow& window, std::vector<sf::Text>& menu, int& selected)
{
    Vector2f mousePos = mapPixelToVirtualScreen(window, Mouse::getPosition(window));
    bool is_any_hovered = false;
    for (size_t i = 0; i < menu.size(); i++)
    {
        if (menu[i].getGlobalBounds().contains(mousePos))
        {
            menu[i].setFillColor(Color::Red);
            selected = static_cast<int>(i);
            is_any_hovered = true;
        }
        else
            menu[i].setFillColor(Color::White);
    }
    if (!is_any_hovered)
        selected = -1;
}

void settexture(RectangleShape sprite[5][2], Texture texture[5][2])
{
    for (int i = 0; i < 2; i++)
        for (int z = 0; z < 5; z++)
            sprite[z][i].setTexture(&texture[z][i]);
}

void setup_textures_and_sprites_boys(Texture boys_profile_texture[5][2], RectangleShape boys_profile_sprite[5][2])
{
    loadTextureSafe(boys_profile_texture[0][0], "boy pro1.JPG");
    loadTextureSafe(boys_profile_texture[1][0], "boy pro2.JPEG");
    loadTextureSafe(boys_profile_texture[2][0], "boy pro3.JPEG");
    loadTextureSafe(boys_profile_texture[3][0], "boy pro4.JPEG");
    loadTextureSafe(boys_profile_texture[4][0], "boy pro5.JPEG");
    loadTextureSafe(boys_profile_texture[0][1], "boy pro6.JPEG");
    loadTextureSafe(boys_profile_texture[1][1], "boy pro7.JPEG");
    loadTextureSafe(boys_profile_texture[2][1], "boy pro8.JPEG");
    loadTextureSafe(boys_profile_texture[3][1], "boy pro9.JPG");
    loadTextureSafe(boys_profile_texture[4][1], "boy pro10.JPG");
    settexture(boys_profile_sprite, boys_profile_texture);
}

void setup_textures_and_sprites_girls(Texture girls_profile_texture[5][2], RectangleShape girls_profile_sprite[5][2])
{
    loadTextureSafe(girls_profile_texture[0][0], "girl pro1.JPG");
    loadTextureSafe(girls_profile_texture[1][0], "girl pro2.JPG");
    loadTextureSafe(girls_profile_texture[2][0], "girl pro3.JPG");
    loadTextureSafe(girls_profile_texture[3][0], "girl pro4.JPG");
    loadTextureSafe(girls_profile_texture[4][0], "girl pro5.JPG");
    loadTextureSafe(girls_profile_texture[0][1], "girl pro6.JPG");
    loadTextureSafe(girls_profile_texture[1][1], "girl pro7.JPG");
    loadTextureSafe(girls_profile_texture[2][1], "girl pro8.JPG");
    loadTextureSafe(girls_profile_texture[3][1], "girl pro9.JPG");
    loadTextureSafe(girls_profile_texture[4][1], "girl pro10.JPG");
    settexture(girls_profile_sprite, girls_profile_texture);
}

void setupSprites_size(RectangleShape sprites[5][2], float size)
{
    for (int i = 0; i < 2; i++)
    {
        for (int z = 0; z < 5; z++)
        {
            sprites[z][i].setSize({ size, size });
            sprites[z][i].setOrigin({ static_cast<float>(size * .5f), static_cast<float>(size * .5f) });
            sprites[z][i].setScale({ 1.1f, 1.1f });
            sprites[z][i].setPosition({ 288.f + z * 333.f, 660.f + i * 300.f });
        }
    }
}
void draw_photos(RenderWindow& window, RectangleShape sprite[5][2], UserRecord& u, int& chosen_photo_z, int& chosen_photo_i)
{
    Vector2f mousePos = mapPixelToVirtualScreen(window, Mouse::getPosition(window));
    for (int i = 0; i < 2; i++)
    {
        for (int z = 0; z < 5; z++)
        {
            if (sprite[z][i].getGlobalBounds().contains(mousePos))
            {
                u.imageNumber = z + i * 5;
                sprite[z][i].setFillColor(Color(200, 200, 200));
            }
            else
                sprite[z][i].setFillColor(Color::White);
            window.draw(sprite[z][i]);
        }
    }
    if (chosen_photo_z >= 0 && chosen_photo_i >= 0)
    {
        Vector2f original_pos = sprite[chosen_photo_z][chosen_photo_i].getPosition();
        sprite[chosen_photo_z][chosen_photo_i].setPosition({ 720.f, 200.f });
        sprite[chosen_photo_z][chosen_photo_i].setScale({ 2.f, 2.f });
        sprite[chosen_photo_z][chosen_photo_i].setFillColor(Color::White);
        window.draw(sprite[chosen_photo_z][chosen_photo_i]);
        sprite[chosen_photo_z][chosen_photo_i].setPosition(original_pos);
        sprite[chosen_photo_z][chosen_photo_i].setScale({ 1.f, 1.f });
    }
}
void draw_photos_outside(RenderWindow& window, RectangleShape sprite[5][2], int& chosen_photo_z, int& chosen_photo_i)
{
    if (chosen_photo_z >= 0 && chosen_photo_i >= 0)
    {
        Vector2f original_pos = sprite[chosen_photo_z][chosen_photo_i].getPosition();
        sprite[chosen_photo_z][chosen_photo_i].setPosition({ 1640.f, 640.f });
        sprite[chosen_photo_z][chosen_photo_i].setScale({ 1.5f, 1.5f });
        sprite[chosen_photo_z][chosen_photo_i].setFillColor(Color::White);
        window.draw(sprite[chosen_photo_z][chosen_photo_i]);
        sprite[chosen_photo_z][chosen_photo_i].setPosition(original_pos);
        sprite[chosen_photo_z][chosen_photo_i].setScale({ 1.3f, 1.3f });
    }
}
void setup_background_color(RectangleShape sprite[5], Texture texture[5])
{
    for (int i = 0; i < 5; i++)
    {
        sprite[i].setSize({ 400.f, 120.f });
        sprite[i].setPosition({ 960.f, 120.f + i * 120.f });
        sprite[i].setOrigin({ 200.f, 60.f });
    }
}
void draw_background_colors(RenderWindow& window, RectangleShape background_color_profile_sprite[5], UserRecord& u, int& chosen_background_color_i)
{
    Vector2f mousePos = mapPixelToVirtualScreen(window, Mouse::getPosition(window));
    for (int i = 0; i < 5; i++)
    {
        if (background_color_profile_sprite[i].getGlobalBounds().contains(mousePos))
        {
            u.barColor = i;
            background_color_profile_sprite[i].setFillColor(Color(200, 200, 200));
        }
        else
            background_color_profile_sprite[i].setFillColor(Color::White);
        window.draw(background_color_profile_sprite[i]);
    }
}
void draw_background_colors_outside(RenderWindow& window, RectangleShape sprite[5], int& chosen_background_color_i)
{
    if (chosen_background_color_i >= 0 && chosen_background_color_i < 5)
    {
        Vector2f original_pos = sprite[chosen_background_color_i].getPosition();
        sprite[chosen_background_color_i].setPosition({ 1465.f, 400.f });
        sprite[chosen_background_color_i].setSize({ 666.f, 756.f });
        sprite[chosen_background_color_i].setFillColor(Color::White);
        window.draw(sprite[chosen_background_color_i]);
        sprite[chosen_background_color_i].setPosition(original_pos);
        sprite[chosen_background_color_i].setSize({ 400.f, 120.f });
    }
}

int main()
{
    sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    RenderWindow window(desktop, "Istanbul Cheese", sf::State::Fullscreen);
    window.setFramerateLimit(144);
    window.setView(makeVirtualScreenView(window));

    if (!ZombieMode::loadZombieTextures())
        std::cout << "Warning: Could not load some zombie textures.\n";

    UserRecord user;
    vector<UserRecord> users;
    string error;
    int currentUserIndex = -1;
    if (loadUsers("users.txt", users, &error))
    {
        for (size_t i = 0; i < users.size(); ++i)
        {
            if (users[i].signedIn)
            {
                user = users[i];
                currentUserIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (currentUserIndex == -1)
    {
        user.id = nextUserId(users);
        user.name = "New User";
        user.email = "";
        user.imageNumber = -1;
        user.barColor = 0;
        user.signedIn = false;
    }

    Font text, text2;
    openFontSafe(text, "Pixelmax-Regular.otf");
    openFontSafe(text2, "PixelCaps!.otf");
    int menucounter = user.signedIn ? 0 : 26, selected = -1, back = -1, chosen_photo_z = -1, chosen_photo_i = -1, chosen_background_color_i = 0, music_played_now = 0, music_played_last = -1;

    std::vector<sf::Text> main_menu_text, pause_menu_text, settings_text, gametypetext, onlinegamestext, offlinegamestext, PvP, PvE, musictext, sound_effects_text, game_profile_text, gender_text, info_text, sign_in_text, sign_or_log_txt;

    setupMenu(main_menu_text, { "play", "settings", "quit" }, &text);
    main_menu_text[1].setPosition({ 760.f, 420.f + 1 * 100.f });
    for (size_t i = 0; i < main_menu_text.size(); i++)
        main_menu_text[i].setPosition({ 840.f, 420.f + (float)i * 120.f });
    setupMenu(pause_menu_text, { "resume", "settings", "quit" }, &text);
    setupMenu(settings_text, { "music", "sound effects", "game profile", "info", "sign", "back" }, &text);
    setupMenu(gametypetext, { "online", "offline", "back" }, &text);
    setupMenu(onlinegamestext, { "PVP", "PVE", "back" }, &text);
    setupMenu(PvP, { "Team deathmatch", "Capture the flag", "back" }, &text);
    setupMenu(PvE, { "Bossfight", "Zombie survival", "back" }, &text);
    setupMenu(offlinegamestext, { "Bossfight", "Zombie survival", "back" }, &text);
    setupMenu(musictext, { "increase", "decrease", "back" }, &text);
    setupMenu(sound_effects_text, { "increase", "decrease", "back" }, &text);
    setupMenu(game_profile_text, { "photo", "user name", "Background profile color", "back" }, &text);
    setupMenu(gender_text, { "man", "woman", "back" }, &text);
    setupMenu(info_text, { "Characters", "enemies", "weapons", "back" }, &text);
    setupMenu(sign_in_text, { "sign in", "sign out", "back" }, &text);
    setupMenu(sign_or_log_txt, { "sign in", "log in" }, &text);
    for (size_t i = 0; i < 2; i++)
    {
        sign_or_log_txt[i].setPosition({ 840.f, 420.f + (float)i * 120.f });
        sign_or_log_txt[i].setCharacterSize(50);
    }

    sf::Text enter_gmail_indicator_text(text);
    enter_gmail_indicator_text.setString("Enter your gmail :");
    enter_gmail_indicator_text.setCharacterSize(40);
    enter_gmail_indicator_text.setPosition({ 270, 480 });
    enter_gmail_indicator_text.setFillColor(Color::White);
    sf::Text enter_username_indicator_text(text);
    enter_username_indicator_text.setString("Enter your username :");
    enter_username_indicator_text.setCharacterSize(40);
    enter_username_indicator_text.setPosition({ 770, 820 });
    enter_username_indicator_text.setFillColor(Color::White);
    sf::Text choose_photo_indicator_text(text);
    choose_photo_indicator_text.setString("Choose your profile photo :");
    choose_photo_indicator_text.setCharacterSize(40);
    choose_photo_indicator_text.setPosition({ 270, 108 });
    choose_photo_indicator_text.setFillColor(Color::White);
    sf::Text usernametext(text2);
    usernametext.setString(user.name);
    usernametext.setCharacterSize(40);
    usernametext.setPosition({ 1400, 820 });
    usernametext.setFillColor(Color::White);
    usernametext.setScale({ .7f, .7f });
    sf::Text usergmailtext(text2);
    usergmailtext.setString(user.email);
    usergmailtext.setCharacterSize(40);
    usergmailtext.setPosition({ 270, 588 });
    usergmailtext.setFillColor(Color::White);
    usergmailtext.setScale({ .7f, .7f });

    RectangleShape box({ 546, 84 });
    box.setPosition({ 1330, 800 });
    box.setFillColor(Color::Black);
    box.setOutlineColor(Color::White);
    box.setOutlineThickness(3);
    RectangleShape boxg({ 1480, 84 });
    boxg.setPosition({ 960, 600 });
    boxg.setOrigin({ 740, 42 });
    boxg.setFillColor(Color::Black);
    boxg.setOutlineColor(Color::White);
    boxg.setOutlineThickness(3);
    RectangleShape soundbarbg({ 620, 80 });
    soundbarbg.setPosition({ 1330, 465 });
    soundbarbg.setFillColor(Color::Black);
    soundbarbg.setOrigin({ 310, 40 });
    soundbarbg.setOutlineColor(Color::White);
    soundbarbg.setOutlineThickness(5);

    RectangleShape sfxbar, musicbar;
    float size = 160.f;
    Texture boys_profile_texture[5][2], girls_profile_texture[5][2];
    RectangleShape boys_profile_sprite[5][2], girls_profile_sprite[5][2];
    setup_textures_and_sprites_boys(boys_profile_texture, boys_profile_sprite);
    setupSprites_size(boys_profile_sprite, size);
    setup_textures_and_sprites_girls(girls_profile_texture, girls_profile_sprite);
    setupSprites_size(girls_profile_sprite, size);

    Texture background_color_profile_texture[5];
    RectangleShape background_color_profile_sprite[5];
    loadTextureSafe(background_color_profile_texture[0], "bkg.png");
    loadTextureSafe(background_color_profile_texture[1], "bkr.png");
    loadTextureSafe(background_color_profile_texture[2], "bky.png");
    loadTextureSafe(background_color_profile_texture[3], "bkb.png");
    loadTextureSafe(background_color_profile_texture[4], "bko.png");
    for (int i = 0; i < 5; i++)
        background_color_profile_sprite[i].setTexture(&background_color_profile_texture[i]);
    setup_background_color(background_color_profile_sprite, background_color_profile_texture);

    Texture game_name_texture;
    loadTextureSafe(game_name_texture, "image-Photoroom.png");
    Sprite game_name_sprite(game_name_texture);
    game_name_sprite.setColor(Color(255, 255, 255, 200));
    game_name_sprite.setOrigin({ 640, 333 });
    game_name_sprite.setPosition({ 960, 180 });
    game_name_sprite.setScale({ 0.9f, 0.9f });
    Texture background_texture;
    loadTextureSafe(background_texture, "background.png");
    Sprite background_sprite(background_texture);
    background_sprite.setColor(Color(255, 255, 255, 200));
    background_sprite.setOrigin({ 480, 269.5f });
    background_sprite.setPosition({ 480, 269.5f });
    background_sprite.setScale({ 3.f, 3.5f });
    Texture profile_userimage_tex;
    loadTextureSafe(profile_userimage_tex, "userimage.png");
    RectangleShape profile_userimage_sprite({ 234, 234 });
    profile_userimage_sprite.setTexture(&profile_userimage_tex);
    profile_userimage_sprite.setPosition({ 1640.f, 640.f });
    profile_userimage_sprite.setOrigin({ 117, 117 });
    profile_userimage_sprite.setScale({ 1.3f, 1.3f });

    float music_volume = 20.f, sound_effects_volume = 50.f;
    SoundBuffer sfxbmenu;
    loadSoundBufferSafe(sfxbmenu, "sfxmenu.MP3");
    Sound sfxmenu(sfxbmenu);
    Music music;
    music.setVolume(music_volume);
    bool ispaused = false, is_main_menu = true, is_profile_selected = false, username_entered = false, typing = false, typing_gmail = false, background_color_selected = false;
    user.gender = false;

    while (window.isOpen())
    {

        // ================== ONLINE / OFFLINE GAMES INTEGRATION ==================
        if (menucounter == 21 && !ispaused)
        {
            music.stop();
            music_played_last = -1;
            ZombieMode::runZombieGame(window, menucounter);
            continue;
        }
        else if (menucounter == 20 && !ispaused)
        {
            music.stop();
            music_played_last = -1;
            BossMode::runBossGame(window, menucounter);
            continue;
        }
        else if (menucounter == 17 && !ispaused)
        {
            music.stop();
            music_played_last = -1;
            OnlineMode::runOnlineGame(window, menucounter, false);
            continue;
        } // TDM
        else if (menucounter == 18 && !ispaused)
        {
            music.stop();
            music_played_last = -1;
            OnlineMode::runOnlineGame(window, menucounter, true);
            continue;
        } // Capture

        while (const std::optional<sf::Event> optEvent = window.pollEvent())
        {
            const sf::Event& event = *optEvent;
            if (event.is<sf::Event::Closed>())
                window.close();
            if (const auto* key = event.getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Escape)
                {
                    ispaused = true;
                    is_main_menu = false;
                    sfxmenu.play();
                }
                if (key->code == sf::Keyboard::Key::Enter)
                {
                    if (menucounter == 9)
                    {
                        menucounter = is_profile_selected ? 0 : 8;
                        typing = false;
                        username_entered = (user.name.size() > 0);
                        sfxmenu.play();
                        if (user.signedIn && currentUserIndex >= 0)
                        {
                            users[currentUserIndex].name = user.name;
                            saveUsers("users.txt", users, &error);
                        }
                    }
                    else if (menucounter == 24)
                    {
                        menucounter = 0;
                        typing_gmail = false;
                        string domain = "@gmail.com";
                        if (user.email.length() > domain.length() && user.email.substr(user.email.length() - domain.length()) == domain)
                        {
                            int idx = findUserIndexByEmail(users, user.email);
                            if (idx >= 0)
                            {
                                users[idx].signedIn = true;
                                user = users[idx];
                                currentUserIndex = idx;
                            }
                            else
                            {
                                user.id = nextUserId(users);
                                user.signedIn = true;
                                users.push_back(user);
                                currentUserIndex = static_cast<int>(users.size()) - 1;
                            }
                            saveUsers("users.txt", users, &error);
                        }
                        else
                        {
                            user.signedIn = false;
                            user.email = "";
                            usergmailtext.setString("");
                        }
                        sfxmenu.play();
                    }
                }
            }
            if (const auto* textEv = event.getIf<sf::Event::TextEntered>())
            {
                if (typing_gmail && menucounter == 24)
                {
                    if (textEv->unicode == '\b' && user.email.size() > 0)
                        user.email.pop_back();
                    else if (textEv->unicode < 128 && user.email.size() < 345 && textEv->unicode != '\b')
                        user.email += static_cast<char>(textEv->unicode);
                    usergmailtext.setString(user.email);
                    string domain = "@gmail.com";
                    user.signedIn = (user.email.length() > domain.length() && user.email.substr(user.email.length() - domain.length()) == domain);
                }
                if (typing && menucounter == 9)
                {
                    if (textEv->unicode == '\b' && user.name.size() > 0)
                        user.name.pop_back();
                    else if (textEv->unicode < 128 && user.name.size() < 11 && textEv->unicode != '\b')
                        user.name += static_cast<char>(textEv->unicode);
                    usernametext.setString(user.name);
                    username_entered = (user.name.size() > 0);
                }
            }
            if (const auto* mouseBtn = event.getIf<sf::Event::MouseButtonPressed>())
            {
                if (mouseBtn->button == sf::Mouse::Button::Left)
                {
                    Vector2f mouseClickPos = mapPixelToVirtualScreen(window, mouseBtn->position);
                    if (!ispaused)
                    {
                        if (menucounter == 9)
                        {
                            typing = box.getGlobalBounds().contains(mouseClickPos);
                            if (!typing && user.signedIn && currentUserIndex >= 0)
                            {
                                users[currentUserIndex].name = user.name;
                                saveUsers("users.txt", users, &error);
                            }
                        }
                        if (menucounter == 24)
                        {
                            typing_gmail = boxg.getGlobalBounds().contains(mouseClickPos);
                            if (!typing_gmail)
                            {
                                string domain = "@gmail.com";
                                if (user.email.length() > domain.length() && user.email.substr(user.email.length() - domain.length()) == domain)
                                {
                                    int idx = findUserIndexByEmail(users, user.email);
                                    if (idx >= 0)
                                    {
                                        users[idx].signedIn = true;
                                        user = users[idx];
                                        currentUserIndex = idx;
                                    }
                                    else
                                    {
                                        user.id = nextUserId(users);
                                        user.signedIn = true;
                                        users.push_back(user);
                                        currentUserIndex = static_cast<int>(users.size()) - 1;
                                    }
                                    saveUsers("users.txt", users, &error);
                                }
                                else
                                {
                                    user.signedIn = false;
                                    user.email = "";
                                    usergmailtext.setString("");
                                }
                            }
                        }
                        switch (menucounter)
                        {
                        case 0:
                        {
                            int clicked = getClickedItem(main_menu_text, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 1;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 2;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                                window.close();
                            back = menucounter;
                            break;
                        }
                        case 2:
                        {
                            int clicked = getClickedItem(settings_text, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 3;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 4;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 7;
                                sfxmenu.play();
                            }
                            else if (clicked == 3)
                            {
                                menucounter = 12;
                                sfxmenu.play();
                            }
                            else if (clicked == 4)
                            {
                                menucounter = 23;
                                sfxmenu.play();
                            }
                            else if (clicked == 5)
                            {
                                menucounter = is_main_menu ? 0 : back;
                                is_main_menu = true;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 1:
                        {
                            int clicked = getClickedItem(gametypetext, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 5;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 6;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 0;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 3:
                        {
                            int clicked = getClickedItem(musictext, mouseClickPos);
                            if (clicked == 0)
                            {
                                music_volume = min(music_volume + 10.f, 100.f);
                                music.setVolume(music_volume);
                            }
                            else if (clicked == 1)
                            {
                                music_volume = max(music_volume - 10.f, 0.f);
                                music.setVolume(music_volume);
                            }
                            else if (clicked == 2)
                                menucounter = 2;
                            back = menucounter;
                            break;
                        }
                        case 4:
                        {
                            int clicked = getClickedItem(sound_effects_text, mouseClickPos);
                            if (clicked == 0)
                            {
                                sound_effects_volume = min(sound_effects_volume + 10.f, 100.f);
                                sfxmenu.setVolume(sound_effects_volume);
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                sound_effects_volume = max(sound_effects_volume - 10.f, 0.f);
                                sfxmenu.setVolume(sound_effects_volume);
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 2;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 5:
                        {
                            int clicked = getClickedItem(onlinegamestext, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 16;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 19;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 1;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 6:
                        {
                            int clicked = getClickedItem(offlinegamestext, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 20;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 21;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 1;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 7:
                        {
                            int clicked = getClickedItem(game_profile_text, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 8;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 9;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 22;
                                sfxmenu.play();
                            }
                            else if (clicked == 3)
                            {
                                menucounter = 2;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 8:
                        {
                            int clicked = getClickedItem(gender_text, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 10;
                                user.gender = 0;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 11;
                                user.gender = 1;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 7;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 10:
                        case 11:
                        {
                            if (user.imageNumber >= 0 && user.imageNumber <= 9)
                            {
                                chosen_photo_z = user.imageNumber % 5;
                                chosen_photo_i = user.imageNumber / 5;
                                is_profile_selected = true;
                                if (user.signedIn && currentUserIndex >= 0)
                                {
                                    users[currentUserIndex].imageNumber = user.imageNumber;
                                    saveUsers("users.txt", users, &error);
                                }
                                menucounter = user.signedIn ? 0 : 24;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 12:
                        {
                            int clicked = getClickedItem(info_text, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 13;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 14;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 15;
                                sfxmenu.play();
                            }
                            else if (clicked == 3)
                            {
                                menucounter = 2;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 16:
                        {
                            int clicked = getClickedItem(PvP, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 17;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 18;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 5;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 19:
                        {
                            int clicked = getClickedItem(PvE, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 20;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 21;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 5;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 22:
                        {
                            if (user.barColor >= 0 && user.barColor < 5)
                            {
                                chosen_background_color_i = user.barColor;
                                background_color_selected = true;
                                if (user.signedIn && currentUserIndex >= 0)
                                {
                                    users[currentUserIndex].barColor = user.barColor;
                                    saveUsers("users.txt", users, &error);
                                }
                                menucounter = user.signedIn ? 0 : 24;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 23:
                        {
                            int clicked = getClickedItem(sign_in_text, mouseClickPos);
                            if (clicked == 0)
                            {
                                menucounter = 24;
                                sfxmenu.play();
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 25;
                                sfxmenu.play();
                            }
                            else if (clicked == 2)
                            {
                                menucounter = 2;
                                sfxmenu.play();
                            }
                            back = menucounter;
                            break;
                        }
                        case 25:
                        {
                            if (currentUserIndex >= 0)
                            {
                                users[currentUserIndex].signedIn = false;
                                saveUsers("users.txt", users, &error);
                                currentUserIndex = -1;
                            }
                            user.signedIn = false;
                            user.email = "";
                            user.name = "";
                            user.imageNumber = -1;
                            user.barColor = 0;
                            chosen_photo_z = -1;
                            chosen_photo_i = -1;
                            chosen_background_color_i = 0;
                            is_profile_selected = false;
                            background_color_selected = false;
                            menucounter = 26;
                            break;
                        }
                        case 26:
                        {
                            int clicked = getClickedItem(sign_or_log_txt, mouseClickPos);
                            if (clicked == 0)
                            {
                                if (!username_entered)
                                    menucounter = 9;
                                else if (!is_profile_selected)
                                {
                                    sfxmenu.play();
                                    menucounter = 8;
                                }
                                else if (!background_color_selected)
                                {
                                    sfxmenu.play();
                                    menucounter = 22;
                                }
                                else if (!user.signedIn)
                                {
                                    sfxmenu.play();
                                    menucounter = 24;
                                }
                            }
                            else if (clicked == 1)
                            {
                                menucounter = 0;
                                sfxmenu.play();
                            }
                            break;
                        }
                        }
                    }
                    else
                    {
                        int clicked = getClickedItem(pause_menu_text, mouseClickPos);
                        if (clicked == 0)
                        {
                            ispaused = false;
                            is_main_menu = true;
                            menucounter = back;
                            sfxmenu.play();
                        }
                        else if (clicked == 1)
                        {
                            menucounter = 2;
                            ispaused = false;
                            sfxmenu.play();
                        }
                        else if (clicked == 2)
                        {
                            menucounter = 0;
                            ispaused = false;
                            is_main_menu = true;
                            sfxmenu.play();
                        }
                    }
                }
            }
        }

        music_played_now = (menucounter == 17 || menucounter == 18) ? 1 : (menucounter == 20) ? 2
            : (menucounter == 21) ? 3
            : 0;
        if (music_played_now != music_played_last)
        {
            music.stop();
            if (music_played_now == 0)
                openMusicSafe(music, "musicmenu.mp3");
            music.setLooping(true);
            music.play();
            music_played_last = music_played_now;
        }

        window.setView(makeVirtualScreenView(window));
        window.clear();
        window.draw(background_sprite);
        if (!ispaused)
        {
            switch (menucounter)
            {
            case 0:
                draw_menu(window, main_menu_text, selected);
                for (auto& t : main_menu_text)
                    window.draw(t);
                window.draw(game_name_sprite);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 1:
                draw_menu(window, gametypetext, selected);
                for (auto& t : gametypetext)
                    window.draw(t);
                window.draw(game_name_sprite);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 2:
                draw_menu(window, settings_text, selected);
                for (auto& t : settings_text)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 3:
                draw_menu(window, musictext, selected);
                for (auto& t : musictext)
                    window.draw(t);
                window.draw(soundbarbg);
                musicbar.setSize(Vector2f(600.f * (music_volume / 100.f), 51.f));
                musicbar.setFillColor(Color::Cyan);
                musicbar.setPosition({ 1030, 440 });
                window.draw(musicbar);
                break;
            case 4:
                draw_menu(window, sound_effects_text, selected);
                for (auto& t : sound_effects_text)
                    window.draw(t);
                window.draw(soundbarbg);
                sfxbar.setSize(Vector2f(600.f * (sound_effects_volume / 100.f), 51.f));
                sfxbar.setFillColor(Color::Cyan);
                sfxbar.setPosition({ 1030, 440 });
                window.draw(sfxbar);
                break;
            case 5:
                draw_menu(window, onlinegamestext, selected);
                for (auto& t : onlinegamestext)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 6:
                draw_menu(window, offlinegamestext, selected);
                for (auto& t : offlinegamestext)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 7:
                draw_menu(window, game_profile_text, selected);
                for (auto& t : game_profile_text)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 8:
                draw_menu(window, gender_text, selected);
                for (auto& t : gender_text)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 9:
                window.draw(enter_username_indicator_text);
                box.setOutlineColor(box.getGlobalBounds().contains(mapPixelToVirtualScreen(window, Mouse::getPosition(window))) ? Color::Yellow : Color::White);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 10:
                window.draw(choose_photo_indicator_text);
                draw_photos(window, boys_profile_sprite, user, chosen_photo_z, chosen_photo_i);
                break;
            case 11:
                window.draw(choose_photo_indicator_text);
                draw_photos(window, girls_profile_sprite, user, chosen_photo_z, chosen_photo_i);
                break;
            case 12:
                draw_menu(window, info_text, selected);
                for (auto& t : info_text)
                    window.draw(t);
                break;
            case 16:
                draw_menu(window, PvP, selected);
                for (auto& t : PvP)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 19:
                draw_menu(window, PvE, selected);
                for (auto& t : PvE)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 22:
                draw_background_colors(window, background_color_profile_sprite, user, chosen_background_color_i);
                break;
            case 23:
                draw_menu(window, sign_in_text, selected);
                for (auto& t : sign_in_text)
                    window.draw(t);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            case 24:
                window.draw(enter_gmail_indicator_text);
                boxg.setOutlineColor(boxg.getGlobalBounds().contains(mapPixelToVirtualScreen(window, Mouse::getPosition(window))) ? Color::Yellow : Color::White);
                window.draw(boxg);
                window.draw(usergmailtext);
                break;
            case 26:
                draw_menu(window, sign_or_log_txt, selected);
                for (auto& t : sign_or_log_txt)
                    window.draw(t);
                window.draw(game_name_sprite);
                draw_background_colors_outside(window, background_color_profile_sprite, chosen_background_color_i);
                window.draw(box);
                window.draw(usernametext);
                break;
            }
            if (!is_profile_selected && menucounter != 10 && menucounter != 11 && menucounter != 24 && menucounter != 3 && menucounter != 4 && menucounter != 13 && menucounter != 14 && menucounter != 15 && menucounter != 17 && menucounter != 18 && menucounter != 20 && menucounter != 21)
                window.draw(profile_userimage_sprite);
            else if (menucounter != 10 && menucounter != 11 && menucounter != 24 && menucounter != 3 && menucounter != 4 && menucounter != 13 && menucounter != 14 && menucounter != 15 && menucounter != 17 && menucounter != 18 && menucounter != 20 && menucounter != 21)
                draw_photos_outside(window, user.gender == 0 ? boys_profile_sprite : girls_profile_sprite, chosen_photo_z, chosen_photo_i);
        }
        else
        {
            draw_menu(window, pause_menu_text, selected);
            for (auto& t : pause_menu_text)
                window.draw(t);
        }
        window.display();
    }
    return 0;
}
