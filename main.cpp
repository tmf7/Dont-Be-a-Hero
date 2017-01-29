//**********************************************
//*
//* Copyright Thomas Freehill January 14 2017
//* Don't Be a Hero (game)
//* 
//**********************************************

#include <Windows.h>
#include "Definitions.h"

#define EMPTY_EXCEPT_SELF(gridCell, entity) (gridCell.contents.empty() || (gridCell.contents.size() == 1 && gridCell.contents[0]->guid == entity->guid))

//----------------------------------------BEGIN DATA STRUCTURES---------------------------------------//

// rendering info
SDL_Window * window;
SDL_Renderer * renderer;
TTF_Font * font;
const SDL_Color clearColor = { 128, 128, 128, 255 };
const SDL_Color transparentGray = { 0, 0, 0, 64 };
const SDL_Color opaqueGreen = { 0, 255, 0, 255 };
const SDL_Color opaqueRed = { 255, 0, 0, 255 };

// debugging info
typedef enum {
	DEBUG_DRAW_COLLISION = BIT(0),
	DEBUG_DRAW_PATH = BIT(1)
} DebugFlags_t;

Uint16 debugState = DEBUG_DRAW_PATH; // DEBUG_DRAW_COLLISION | DEBUG_DRAW_PATH;

//***************
// DebugCheck
//***************
Uint16 DebugCheck(DebugFlags_t flag) {
	return debugState & flag;
}

// map texture
struct {
	SDL_Rect frame;
	SDL_Texture * texture;
} map;

// sprite sheet
struct {
	SDL_Color defaultMod;
	SDL_Texture * texture;
	std::unordered_map<std::string, int> frameAtlas;
	std::vector<SDL_Rect> frames;
} spriteSheet;

// floating point set
typedef struct Vec2_s {
	float x;
	float y;

	float operator[](const int index) const {
		return (&x)[index];
	}

	float & operator[](const int index) {
		return (&x)[index];
	}

	Vec2_s operator*(const float & scale) const {
		return { x * scale, y * scale };
	}

	// dot product
	float operator*(const Vec2_s & rhs) const {
		return x * rhs.x + y * rhs.y;
	}

	Vec2_s operator+(const Vec2_s & rhs) const { 
		return { x + rhs.x, y + rhs.y }; 
	}

	Vec2_s operator-(const Vec2_s & rhs) const {
		return { x - rhs.x, y - rhs.y };
	}

	Vec2_s & operator+=(const Vec2_s & rhs) {
		x += rhs.x;
		y += rhs.y;
		return *this;
	}

} Vec2_t;

Vec2_t vec2zero = { 0.0f, 0.0f };

// forward declaration of GameObject_t
typedef struct GameObject_s GameObject_t;

// GridCell_t
typedef struct GridCell_s {
	// pathfinding
	GridCell_s * parent = nullptr;		// originating cell to set the path back from the goal
	int gCost = 0;				// distance from start cell to this cell
	int hCost = 0;				// distance from this cell to goal
	int fCost = 0;				// sum of gCost and hCost
	int gridRow;				// index within gameGrid
	int gridCol;				// index within gameGrid
	bool inOpenSet = false;		// expidites PathFind openSet searches
	bool inClosedSet = false;	// expidites PathFind closedSet searches

	bool solid;												// triggers collision
	SDL_Rect bounds;										// world location and cell size
	SDL_Point center;										// cached bounds centerpoint for quicker pathfinding
	std::vector<std::shared_ptr<GameObject_t>> contents;	// monsters, missiles, and/or Goodman
} GridCell_t;

// grid dimensions
constexpr const int gameWidth		= 800;
constexpr const int gameHeight		= 600;
constexpr const int cellSize		= 16;	// 16x16 square cells
constexpr const int gridRows		= gameWidth / cellSize;
constexpr const int gridCols		= gameHeight / cellSize;

// gameGrid
struct {
	SDL_Texture * texture;
	std::array<std::array<GridCell_t, gridCols>, gridRows> cells;	// spatial partitioning of play area
} gameGrid;

// selection
std::vector<GridCell_t *> selection;	// all interior and border cells of selected area

// ObjectType_t
typedef enum {
	OBJECTTYPE_INVALID = -1,
	OBJECTTYPE_GOODMAN,
	OBJECTTYPE_MELEE,
	OBJECTTYPE_RANGED,
	OBJECTTYPE_MISSILE
} ObjectType_t;

// GameObject_t
typedef struct GameObject_s {
	SDL_Point		origin;			// top-left of sprite image
	SDL_Rect		bounds;			// world-position and size of collision box
	float			speed;
	Vec2_t			velocity;
	Vec2_t			center;			// center of the collision bounding box

	int				bob;			// the illusion of walking
	bool			bobMaxed;		// peak bob height, return to 0
	Uint32			moveTime;		// delay between move updates

	int				facing;			// left or right to determine flip
	int				health;			// health <= 0 rotates sprite 90 and color-blends gray, hit color-blends red
	int				stamina;		// subtraction color-blends blue for a few frames, stamina <= 0 blinks blue until full
	bool			damaged;
	bool			fatigued;
	Uint32			blinkTime;		// future point to stop color mod

	ObjectType_t	type;			// for faster Think calls
	std::string		name;			// globally unique name (substring can be used for spriteSheet.frameAtlas)
	int				guid;			// globally unique identifier amongst all entites (by number)
	int				groupID;		// entity-group this belongs to (for flocking behavior)

	SDL_Point *						currentWaypoint;
	std::vector<GridCell_t *>		path;				// A* pathfinding results
	std::vector<GridCell_t *>		cells;				// currently occupied gameGrid.cells indexes (between 1 and 4)
	bool							onPath;				// if the entity is on the back tile of its path
	SDL_Point						goal;				// user-defined path objective

	GameObject_s() 
		:	origin({0, 0}),
			bounds({0, 0, 0, 0}),
			center(vec2zero),
			speed(0.0f),
			velocity(vec2zero),
			name("invalid"),
			bob(0),
			bobMaxed(false),
			moveTime(0),
			facing(false),
			blinkTime(0),
			health(0),
			stamina(0),
			damaged(false),
			fatigued(false),
			type(OBJECTTYPE_INVALID),
			guid(-1) {
			currentWaypoint = &origin;
			onPath = false;
	};

	GameObject_s(const SDL_Point & origin,  const std::string & name, const int guid, ObjectType_t type) 
		:	origin(origin),
			velocity(vec2zero),
			name(name),
			bob(0),
			bobMaxed(false),
			moveTime(0),
			facing(rand() % 2),
			blinkTime(0),
			damaged(false),
			fatigued(false),
			type(type),
			guid(guid) {
		currentWaypoint = &(this->origin);
		onPath = false;
		bounds = {origin.x + 4, origin.y + 4, 8, 16 };
		center.x = (float)bounds.x + (float)bounds.w / 2.0f;
		center.y = (float)bounds.y + (float)bounds.h / 2.0f;
		switch (type) {
			case OBJECTTYPE_GOODMAN:
				health = 100;
				stamina = 100;
				speed = 4;
				break; 
			case OBJECTTYPE_MELEE:
			case OBJECTTYPE_RANGED: 
				health = 2;
				stamina = -1;
				speed = 2;
				break;
			case OBJECTTYPE_MISSILE: 
				health = 1;
				stamina = -1;
				speed = 3;
				break;
			default: 
				health = 0;
				stamina = 0;
				speed = 0;
				break;
		}
	};
} GameObject_t;

// entities
std::unordered_map<std::string, int> entityAtlas;		// entity lookup by name
std::vector<std::shared_ptr<GameObject_t>> entities;	// all dynamically allocated game objects
int entityGUID = 0;

// dynamic pathfinding
typedef enum {
	COUNTER_CLOCKWISE = false,
	CLOCKWISE = true
} RotationDirection_t;

//----------------------------------------END DATA STRUCTURES---------------------------------------//
//-------------------------------------BEGIN RENDERING FUNCTIONS------------------------------------//

//***************
// DrawRect
//***************
void DrawRect(const SDL_Rect & rect, const SDL_Color & color, bool fill) {
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

	fill ? SDL_RenderFillRect(renderer, &rect)
		: SDL_RenderDrawRect(renderer, &rect);

	SDL_SetRenderDrawColor(renderer, clearColor.r, clearColor.g, clearColor.b, clearColor.a);
}

//***************
// DrawGameGrid
//***************
void DrawGameGrid() {
	SDL_Rect dstRect = { 0, 0, gameWidth, gameHeight };
	SDL_RenderCopy(renderer, gameGrid.texture, NULL, &dstRect);
}

//***************
// DrawCollision
//***************
void DrawCollision() {
	for (auto && arry : gameGrid.cells)
		for (auto && cell : arry)
			if (cell.solid)
				DrawRect(cell.bounds, opaqueGreen, false);
}

//***************
// DrawSelection
// DEBUG: inefficient debug draw check
//***************
void DrawSelection() {
	for (auto && cell : selection)
		DrawRect(cell->bounds, opaqueGreen, false);
}

//***************
// DrawOutlineText
//***************
void DrawOutlineText(const char * text, const SDL_Point & location, const SDL_Color & color) {
	SDL_Surface * source = TTF_RenderText_Blended(font, text, color);
	if (!source)
		return;

	// transparent text hack
	SDL_SetSurfaceAlphaMod(source, color.a);

	SDL_Texture * renderedText = SDL_CreateTextureFromSurface(renderer, source);
	SDL_FreeSurface(source);
	if (!renderedText)
		return;

	SDL_Rect dstRect;
	SDL_QueryTexture(renderedText, NULL, NULL, &dstRect.w, &dstRect.h);
	dstRect.x = location.x;
	dstRect.y = location.y;

	SDL_RenderCopy(renderer, renderedText, NULL, &dstRect);
	SDL_DestroyTexture(renderedText);
}

//***************
// DrawPath
//***************
void DrawPath(std::shared_ptr<GameObject_t> & entity) {
	for (auto && cell : entity->path) {
		SDL_Color drawColor;
		if (cell == *(entity->path.begin()))	// DEBUG: this should be the point closest to the goal
			drawColor = opaqueRed;
		else if (cell == *(entity->path.rbegin())) // DEBUG: this should be the point closest to the sprite
			drawColor = {25, 128, 255, 255};
		else
			drawColor = opaqueGreen;
		DrawRect(SDL_Rect{ cell->center.x, cell->center.y, 2, 2 }, drawColor, true);
	}
}

//***************
// DrawEntities
// TODO: set blinkTime, health, damaged, and fatigued elsewhere
//***************
void DrawEntities() {

	// inefficient draw-order sort according to y-position
	// makes a copy of all entity pointers to prevent
	// invalidating the entityAtlas
	std::vector<std::shared_ptr<GameObject_t>> drawEntities(entities);
	std::sort(	drawEntities.begin(), 
				drawEntities.end(), 
				[](auto && a, auto && b) { 
					return a->origin.y < b->origin.y;
				}	
	);

	for (auto && entity : drawEntities) {
		// get the drawing frames
		int offset = entity->name.find_first_of('_');
		std::string spriteName = entity->name.substr(0, offset);
		SDL_Rect & srcRect = spriteSheet.frames[spriteSheet.frameAtlas[spriteName]];
		SDL_Rect dstRect = { entity->origin.x, entity->origin.y, srcRect.w, srcRect.h };

		// set color and rotation modifiers
		double angle = 0;
		if (entity->blinkTime > SDL_GetTicks()) {
			const SDL_Color & mod = entity->damaged ? SDL_Color{ 239, 12, 14, SDL_ALPHA_OPAQUE } 
													: (entity->fatigued ? SDL_Color{ 22, 125, 236, SDL_ALPHA_OPAQUE }
																		: spriteSheet.defaultMod);
			SDL_SetTextureColorMod(spriteSheet.texture, mod.r, mod.g, mod.b);
		}
		if (entity->health <= 0) {
			SDL_SetTextureColorMod(spriteSheet.texture, 128, 128, 128);
			angle = entity->facing ? -90 : 90;
		}

		// draw one sprite, then reset modifiers
		SDL_RenderCopyEx(renderer, spriteSheet.texture, &srcRect, &dstRect, angle, NULL, (SDL_RendererFlip)entity->facing);
		SDL_SetTextureColorMod(	spriteSheet.texture, 
								spriteSheet.defaultMod.r, 
								spriteSheet.defaultMod.g, 
								spriteSheet.defaultMod.b	);

		// draw collision box
		if (DebugCheck(DEBUG_DRAW_COLLISION))
			DrawRect(entity->bounds, opaqueGreen, false);

		// draw path
		if (DebugCheck(DEBUG_DRAW_PATH))
			DrawPath(entity);
	}
}

//------------------------------------------END RENDERING FUNCTIONS----------------------------------------//
//-------------------------------------BEGIN INITIALIZATION FUNCTIONS--------------------------------------//

//***************
// BuildGrid
//***************
bool BuildGrid() {
	// initilize the grid texture
	gameGrid.texture = SDL_CreateTexture(	renderer,
											SDL_PIXELFORMAT_ARGB8888,
											SDL_TEXTUREACCESS_TARGET,
											gameWidth,
											gameHeight	);

	if (!gameGrid.texture || SDL_SetTextureBlendMode(gameGrid.texture, SDL_BLENDMODE_BLEND))
		return false;

	if (SDL_SetRenderTarget(renderer, gameGrid.texture))
		return false;

	// cache all grid cell coordinates
	// and copy the grid to the gridTexture for faster drawing
	for (int row = 0; row < gridRows; row++) {
		for (int col = 0; col < gridCols; col++) {
			gameGrid.cells[row][col].gridRow = row;
			gameGrid.cells[row][col].gridCol = col;
			SDL_Rect & bounds = gameGrid.cells[row][col].bounds;
			bounds = { row * cellSize,  col * cellSize, cellSize, cellSize };
			gameGrid.cells[row][col].center = { bounds.x + (bounds.w / 2), bounds.y + (bounds.h / 2) };
			DrawRect(bounds, transparentGray, false);
		}
	}

	if (SDL_SetRenderTarget(renderer, NULL))
		return false;

	return true;
}

//***************
// LoadCollision
//***************
bool LoadCollision() { 
	// for each non-zero set a cell to COLLISION_TILE (true)
	std::ifstream	read("graphics/collision.txt");
	// unable to find/open file
	if(!read.good())
		return false;

	int row = 0;
	int col = 0;
	while (!read.eof()) {
		char token[8];
		read.getline(token, 8, ',');
		if (read.bad() || read.fail()) {
			read.clear();
			read.close();
			return false;
		}

		int value = atoi(token);
		if (value)
			gameGrid.cells[row][col].solid = true;

		row++;
		if (row >= gridRows) {
			row = 0;
			col++;
			if (col >= gridCols)
				break;
		}
	}
	read.close();
	return true;
}

//***************
// LoadSprites
//***************
bool LoadSprites() {
	SDL_Surface * goodman = IMG_Load("graphics/goodman.png");
	if (!goodman)
		return false;

	SDL_Surface * melee = IMG_Load("graphics/melee.png");
	if (!melee) {
		SDL_FreeSurface(goodman);
		return false;
	}

	SDL_Surface * ranged = IMG_Load("graphics/ranged.png");
	if (!ranged) {
		SDL_FreeSurface(goodman);
		SDL_FreeSurface(melee);
		return false;
	}

	SDL_Surface * missile = IMG_Load("graphics/missile.png");
	if (!missile) {
		SDL_FreeSurface(goodman);
		SDL_FreeSurface(melee);
		SDL_FreeSurface(ranged);
		return false;
	}

	// set width and height of spriteSheet for all sprites in a row
	// DEBUG: hardcoded dimensions determined from manual image file comparision
	int textureWidth = 64;
	int textureHeight = 32;

	// initilize the grid texture
	spriteSheet.texture = SDL_CreateTexture(	renderer,
												goodman->format->format,
												SDL_TEXTUREACCESS_TARGET,
												textureWidth,
												textureHeight	);

	if (!spriteSheet.texture || SDL_SetTextureBlendMode(spriteSheet.texture, SDL_BLENDMODE_BLEND))
		return false;

	if (SDL_SetRenderTarget(renderer, spriteSheet.texture))
		return false;

	// build the frame list 
	// and copy the surface to the texture
	SDL_Point origin = { 0, 0 };
	SDL_Rect frame;
	for (int index = 0; index < 4; index++) {
		SDL_Surface * target;
		std::string name;

		switch (index) {
			case 0: target = goodman; name = "goodman"; break;
			case 1: target = melee; name = "melee"; break;
			case 2: target = ranged; name = "ranged"; break;
			case 3: target = missile; name = "missile"; break;
			default: target = NULL; name = "invalid"; break;
		}
		frame = { origin.x, origin.y, target->w, target->h };
		if (SDL_UpdateTexture(spriteSheet.texture, &frame, target->pixels, target->pitch)) {
			SDL_FreeSurface(goodman);
			SDL_FreeSurface(melee);
			SDL_FreeSurface(ranged);
			SDL_FreeSurface(missile);
			return false;
		}
		spriteSheet.frames.push_back(frame);
		spriteSheet.frameAtlas[name] = index;
		origin.x += target->w;
	}	

	if (SDL_SetRenderTarget(renderer, NULL))
		return false;

	SDL_FreeSurface(goodman);
	SDL_FreeSurface(melee);
	SDL_FreeSurface(ranged);
	SDL_FreeSurface(missile);

	// cache the original color mod
	SDL_GetTextureColorMod(	spriteSheet.texture, 
							&spriteSheet.defaultMod.r, 
							&spriteSheet.defaultMod.g, 
							&spriteSheet.defaultMod.b	);
	return true;
}

// forward declaration of UpdateCellReferences
void UpdateCellReferences(std::shared_ptr<GameObject_t> & entity);

//***************
// SpawnGoodman
//***************
void SpawnGoodman() {
	int spawnX = 100;
	int spawnY = 100;
	int spawnRow = spawnX / cellSize;
	int spawnCol = spawnY / cellSize;
	// TODO: make this a random location in the grid (that isn't a collision tile)
	std::string name = "goodman";
	std::shared_ptr<GameObject_t> goodman = std::make_shared<GameObject_t>(SDL_Point{ spawnX, spawnY }, name, entityGUID, OBJECTTYPE_GOODMAN);
	entities.push_back(goodman);
	entityAtlas[name] = entityGUID++;
	UpdateCellReferences(goodman);
}

//***************
// SpawnMonsters
// TODO: cluster spawn points in a region of the map farthest from Goodman
// OR: potentially pick TWO  spawn points, then "animate" the monsters flooding in (which affects strategy)
//***************
void SpawnMonsters() {
	// seed the random number generator
	srand(SDL_GetTicks());

	// spawn all the monsters in one go
	for (int count = 0; count < 20; count++) {
		
		SDL_Point spawnPoint;
		bool invalidSpawnPoint = false;
		do {
			spawnPoint.x = rand() % gameWidth;					
			spawnPoint.y = rand() % (gameHeight - cellSize);	// DEBUG: no grid row along bottom of the screen
			invalidSpawnPoint = false;

			// check the spawnPoint's resulting collision bounds' four corners,
			// which will be over 1 - 4 gameGrid cells, doesn't overlap another entity, a solid cell, and is on the map area
			SDL_Rect collideBounds = { spawnPoint.x + 4, spawnPoint.y + 4, 8, 16 };
			for (int corner = 0; corner < 4; corner++) {
				SDL_Point testPoint;
				switch (corner) {
					case 0: testPoint = { collideBounds.x , collideBounds.y }; break;
					case 1: testPoint = { collideBounds.x + collideBounds.w, collideBounds.y }; break;
					case 2: testPoint = { collideBounds.x , collideBounds.y + collideBounds.h }; break;
					case 3: testPoint = { collideBounds.x + collideBounds.w , collideBounds.y + collideBounds.h}; break;
				}

				int testRow = testPoint.x / cellSize;
				int testCol = testPoint.y / cellSize;
				if (testRow < 0 || testRow >= gridRows || testCol < 0 || testCol >= gridCols) {
					invalidSpawnPoint = true;
					break;
				}

				GridCell_t * cell = &gameGrid.cells[testRow][testCol];
				if (cell->solid) {
					invalidSpawnPoint = true;
					break;
				}

				if (!cell->contents.empty()) {
					invalidSpawnPoint = true;
					break;
				}
			}
		} while (invalidSpawnPoint);

		// give it a type and name
		ObjectType_t type = (ObjectType_t)((rand() % 2) + 1);
		std::string name;
		switch (type) {
			case OBJECTTYPE_MELEE: name = "melee_"; break;
			case OBJECTTYPE_RANGED: name = "ranged_"; break;
		}
		name += std::to_string(entityGUID);

		// add it to the entity vector, entityAtlas, and gameGrid
		std::shared_ptr<GameObject_t> monster = std::make_shared<GameObject_t>(spawnPoint, name, entityGUID, type);
		entities.push_back(monster);
		entityAtlas[name] = entityGUID++;
		UpdateCellReferences(monster);
	}
}

//***************
// InitGame
//***************
bool InitGame(std::string & message) {
	if (SDL_Init(SDL_INIT_EVERYTHING))
		return false;

	window = SDL_CreateWindow(	"Don't Be a Hero!", 
								SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
								gameWidth, gameHeight, 
								SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);

	if (!window) {
		message = "Window";
		return false;
	}

	// DEBUG: TARGETTEXTURE is used to read pixel data from SDL_Textures
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

	if (!renderer) {
		message = "Renderer";
		return false;
	}

	// enable linear anti-aliasing for the renderer context
	SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "linear" , SDL_HINT_OVERRIDE);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	// clear the screen to opaque grey
	if (SDL_SetRenderDrawColor(renderer, clearColor.r, clearColor.g, clearColor.b, clearColor.a)) {
		message = "Draw color";
		return false;
	}

	if (TTF_Init()) {
		message = "Font system";
		return false;
	}

	font = TTF_OpenFont("fonts/brush.ttf", 24);
	if (!font) {
		message = "Font file";
		return false;
	}

	map.texture = IMG_LoadTexture(renderer, "graphics/DontBeAHero.png");
	SDL_SetTextureBlendMode(map.texture, SDL_BLENDMODE_BLEND);
	if (!map.texture) {
		message = "Map texture";
		return false;
	}
	SDL_QueryTexture(map.texture, NULL, NULL, &map.frame.w, &map.frame.h);

	if (!BuildGrid()) {
		message = "Grid";
		return false;
	}

	if (!LoadSprites()) {
		message = "Sprite Sheet";
		return false;
	}

	if (!LoadCollision()) {
		message = "Collision map";
		return false;
	}

	SpawnGoodman();
	SpawnMonsters();

	return true;
}

//-------------------------------------END INITIALIZATION FUNCTIONS--------------------------------------//
//-------------------------------------BEGIN PER-FRAME FUNCTIONS-----------------------------------------//

//***************
// RemoveObject
// returns true on success, 
// false otherwise
//***************
bool RemoveObject(const std::vector<std::shared_ptr<GameObject_t>> & vector, const std::shared_ptr<GameObject_t> & target) {
	for (auto && object : vector) {
		if (object == target) {
			return true;
		}
	}
	return false;
}

//***************
// GetDistance
// A* pathfinding utility
//***************
int GetDistance(GridCell_t * start, GridCell_t * end) {
	int rowDist = SDL_abs(start->gridRow - end->gridRow);
	int colDist = SDL_abs(start->gridCol - end->gridCol);

	if (rowDist > colDist)
		return (14 * colDist + 10 * (rowDist - colDist));
	return (14 * rowDist + 10 * (colDist - rowDist));
}

//***************
// ClearSets
// reset all the searched cells' costs
// and set occupancy bools
// to avoid affecting the next search
//***************
void ClearSets(std::vector<GridCell_t *> & openSet, std::vector<GridCell_t*> & closedSet) {
	// clear the openSet
	for (auto && cell : openSet) {
		cell->gCost = 0;
		cell->hCost = 0;
		cell->fCost = 0;
		cell->inOpenSet = false;
		cell->parent = nullptr;
	}
	openSet.clear();

	// clear the closedSet
	for (auto && cell : closedSet) {
		cell->gCost = 0;
		cell->hCost = 0;
		cell->fCost = 0;
		cell->inClosedSet = false;
		cell->parent = nullptr;
	}
	closedSet.clear();
}

//***************
// PathFind
// A* search of gameGrid cells
// only searches static non-solid geometry
// entities will perform dynamic collision avoidance on the fly
// returns false if no valid path is found (also clears the current path)
// returns true if a valid path was constructed (after clearing the current path)
//***************
bool PathFind(std::shared_ptr<GameObject_t> & entity, SDL_Point & start, SDL_Point & goal) {

	// DEBUG: static to prevent excessive dynamic allocation
	static std::vector<GridCell_t *> openSet;
	static std::vector<GridCell_t *> closedSet;

	int startRow = start.x / cellSize;
	int startCol = start.y / cellSize;
	int endRow = goal.x / cellSize;
	int endCol = goal.y / cellSize;

	// snap off-map start row/col
	if (startRow < 0)
		startRow = 0;
	else if (startRow >= gridRows)
		startRow = gridRows - 1;
	if (startCol < 0)
		startCol = 0;
	else if (startCol >= gridCols)
		startCol = gridCols - 1;

	// snap off-map end row/col
	if (endRow < 0)
		endRow = 0;
	else if (endRow >= gridRows)
		endRow = gridRows - 1;
	if (endCol < 0)
		endCol = 0;
	else if (endCol >= gridCols)
		endCol = gridCols - 1;

	GridCell_t * startCell = &gameGrid.cells[startRow][startCol];
	GridCell_t * endCell = &gameGrid.cells[endRow][endCol];

	if (endCell->solid || startCell == endCell) {
		entity->path.clear();
		entity->currentWaypoint = &entity->origin;
		return false;
	}
	
	// openset sort priority: first by fCost, then by hCost 
	auto cmp = [](auto && a, auto && b) {
		if (a->fCost > b->fCost)
			return true;
		else if (a->fCost == b->fCost)
			return a->hCost > b->hCost;
		return false;
	};

	// pathfinding
	startCell->inOpenSet = true;
	openSet.push_back(startCell);
	while (!openSet.empty()) {
		std::make_heap(openSet.begin(), openSet.end(), cmp);		// heapify by fCost and hCost
		auto currentCell = openSet.front();							// copy the lowest cost cell pointer
		openSet.erase(openSet.begin());								// remove from openSet
		currentCell->inOpenSet = false;
		closedSet.push_back(currentCell);							// add to closedSet using the bounds as the hashkey
		currentCell->inClosedSet = true;

		// check if the path is complete
		if (currentCell == endCell) {

			// ready the path for updating
			entity->path.clear();

			// build the path back (reverse iterator)
			while (currentCell != nullptr) {		// FIXME: was currentCell->parent != nullptr, but that excluded the starting cell
				entity->path.push_back(currentCell);		// TODO: instead push a flowVelocity at this index (maybe?)
				currentCell = currentCell->parent;
			}

			// set the the local goal
			entity->currentWaypoint = &entity->path.back()->center;
			entity->onPath = true;

			// ensure no GridCell_t conflicts on successive path searches
			ClearSets(openSet, closedSet);

			return true;
		}

		// traverse the current cell's neighbors
		// updating costs and adding to the openSet as needed 
		// DEBUG: avoid the cell itself, offmap cells, solid cells, OCCUPIED cells, and closedSet cells, respectively
		for (int row = -1; row <= 1; row++) {
			for (int col = -1; col <= 1; col++) {
				int nRow = currentCell->gridRow + row;
				int nCol = currentCell->gridCol + col;
				
				// check for invalid neighbors
				if ((row == 0 && col == 0) ||
					(nRow < 0 || nRow >= gridRows || nCol < 0 || nCol >= gridCols) ||
					gameGrid.cells[nRow][nCol].solid ||
//					!EMPTY_EXCEPT_SELF(gameGrid.cells[nRow][nCol], entity) ||
					gameGrid.cells[nRow][nCol].inClosedSet) {
					continue;
				}

				// check for updated gCost or entirely new cell
				GridCell_t * neighbor = &gameGrid.cells[nRow][nCol];
				int gCost = currentCell->gCost + GetDistance(currentCell, neighbor);
				if (gCost < neighbor->gCost || !neighbor->inOpenSet) {
					neighbor->gCost = gCost;
					neighbor->hCost = GetDistance(neighbor, endCell);
					neighbor->fCost = gCost + neighbor->hCost;
					neighbor->parent = currentCell;

					if (!neighbor->inOpenSet) {
						openSet.push_back(neighbor);
						neighbor->inOpenSet = true;
					}
				}
			}
		}
	}
	// ensure no GridCell_t conflicts on successive path searches
	ClearSets(openSet, closedSet);

	entity->path.clear();
	entity->currentWaypoint = &entity->origin;
	return false;		// DEBUG: this will be hit if an entity is surrounded by entities on its first search
}

/*
//***************
// UpdateCollision
//***************
void UpdateCollision() {
	// one at at time, such that the first entity's final position is resolved before the second entity moves
	// to check for collision, then adjust its final position.
	SDL_HasIntersection(NULL, NULL);						// if this does occur, then clip a line segment that travels
	// from 
	SDL_IntersectRectAndLine(NULL, NULL, NULL, NULL, NULL); // segment gets clipped in EITHER direction
	// traverse all entities and add them to each grid cell their bounds is over (likely just one)
	// similarly, add each cell under the entity bounds to the entity's list
	// ultimately, each frame, traverse each entity that has MOVED (no bobbing in place) and check
	// all the lists of cells it belongs to for any collision boxes (wall, goodman, monster, missile)
}
*/

//***************
// UpdateBob
// used for animation
//***************
void UpdateBob(std::shared_ptr<GameObject_t> & entity, const Vec2_t & move) {
	if (entity->health > 0) {
		bool continueBob = (move.x || move.y);
		if (continueBob && !entity->bobMaxed) {
			entity->bobMaxed = (++entity->bob >= 5) ? true : false;
			entity->origin.y--;
		} else if (entity->bob > 0) {
			entity->bobMaxed = (--entity->bob > 0) ? true : false;
			entity->origin.y++;
		}
	}
}

//***************
// CheckWaypointRange
// used for dynamic pathfinding
//***************
bool CheckWaypointRange(std::shared_ptr<GameObject_t> & entity) {
	int xRange = SDL_abs((int)(entity->center.x - entity->currentWaypoint->x));
	int yRange = SDL_abs((int)(entity->center.y - entity->currentWaypoint->y));
	return (xRange <= entity->speed && yRange <= entity->speed);
}

//***************
// Normalize
// used for dynamic pathfinding
//***************
void Normalize(Vec2_t & v) {
	float length = SDL_sqrtf(v.x * v.x + v.y * v.y);
	if (length) {
		float invLength = 1.0f / length;
		v.x *= invLength;
		v.y *= invLength;
	}
}

//***************
// UpdateOrigin
//***************
void UpdateOrigin(std::shared_ptr<GameObject_t>  & entity, const Vec2_t & minMove) {
	int dx = (int)nearbyintf(minMove.x);	// (int)nearbyintf(entity->speed * entity->velocity.x);
	int dy = (int)nearbyintf(minMove.y);	// (int)nearbyintf(entity->speed * entity->velocity.y);

	entity->origin.x += dx;
	entity->origin.y += dy;
	entity->bounds.x += dx;
	entity->bounds.y += dy;
	entity->center.x += nearbyintf(minMove.x);	// nearbyintf(entity->speed * entity->velocity.x);
	entity->center.y += nearbyintf(minMove.y);	// nearbyintf(entity->speed * entity->velocity.y);
}

//***************
// UpdateCellReferences
// collision utitliy
//***************
void UpdateCellReferences(std::shared_ptr<GameObject_t> & entity) {
	// remove the entity from any gameGrid.cells its currently in
	for (auto && cell : entity->cells) {
		auto & index = std::find(cell->contents.begin(), cell->contents.end(), entity);
		if (index != cell->contents.end())
			cell->contents.erase(index);
	}

	// empty the entity's cell references
	entity->cells.clear();

	// check entity->bounds' four corners
	// entity will wind up with 1 - 4 gameGrid cell references
	// and those same gameGrid cells will have references to the entity
	for (int corner = 0; corner < 4; corner++) {
		SDL_Point testPoint;
		switch (corner) {
			case 0: testPoint = { entity->bounds.x , entity->bounds.y }; break;
			case 1: testPoint = { entity->bounds.x + entity->bounds.w, entity->bounds.y }; break;
			case 2: testPoint = { entity->bounds.x , entity->bounds.y + entity->bounds.h }; break;
			case 3: testPoint = { entity->bounds.x + entity->bounds.w , entity->bounds.y + entity->bounds.h}; break;
		}

		// update the entity->cells
		int testRow = testPoint.x / cellSize;
		int testCol = testPoint.y / cellSize;
		if (testRow < 0 || testRow >= gridRows || testCol < 0 || testCol >= gridCols)
			continue;

		GridCell_t * cell = &gameGrid.cells[testRow][testCol];
		if (cell->solid)
			continue;

		if (std::find(entity->cells.begin(), entity->cells.end(), cell) == entity->cells.end()) 
			entity->cells.push_back(cell);
	}

	// add the entity to any gameGrid cells its currently over
	for (auto && cell : entity->cells) {
		cell->contents.push_back(entity);
	}
}

//***************
// GetAreaContents
// dynamic pathfinding utility
// queries area dynamic contents
//***************
void GetAreaContents(const int centerRow, const int centerCol, std::vector<std::shared_ptr<GameObject_t>> & result, std::shared_ptr<GameObject_t> & ignore) {

	static std::vector<int> idList;

	for (int row = -1; row <= 1; row++) {
		for(int col = -1; col <= 1; col++) {
			int checkRow = centerRow + row;
			int checkCol = centerCol + col;
			if (checkRow >= 0 && checkRow < gridRows && checkCol >= 0 && checkCol < gridCols) {
				auto & target = gameGrid.cells[checkRow][checkCol];
				if (!target.solid) {

					for (auto && entity : target.contents) {
						if (entity->guid == ignore->guid)
							continue;

						// DEBUG: don't add the same entity twice for those over multiple cells
						auto & checkID = std::find(idList.begin(), idList.end(), entity->guid);
						if (checkID == idList.end()) {
							result.push_back(entity);
							idList.push_back(entity->guid);
						}
					}

				}
			}
		}
	}

	idList.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////
// BEGIN FREEHILL flocking test

//***************
// GetGroupAlignment
// flocking utility
// calculates the normalized average group velocity
// TODO(?): weighted average
//***************
void GetGroupAlignment(std::vector<std::shared_ptr<GameObject_t>> & areaContents, std::shared_ptr<GameObject_t> & self, Vec2_t & result) {
	result = vec2zero;
	if (!areaContents.size())
		return;

	float groupCount = 0.0f;
	for (auto && entity : areaContents) {
		if (entity->groupID == self->groupID) {
			result.x += entity->velocity.x;
			result.y += entity->velocity.y;
			groupCount++;
		}
	}

	if (!groupCount)
		return;

	result.x /= groupCount;
	result.y /= groupCount;
	Normalize(result);
}
//***************
// GetGroupCohesion
// flocking utility
// calulates the average group center
// then a normalized vector towards that center
//***************
void GetGroupCohesion(std::vector<std::shared_ptr<GameObject_t>> & areaContents, std::shared_ptr<GameObject_t> & self, Vec2_t & result) {
	result = vec2zero;
	if (!areaContents.size())
		return;

	float groupCount = 0.0f;
	for (auto && entity : areaContents) {
		if (entity->groupID == self->groupID) {
			result.x += entity->center.x;
			result.y += entity->center.y;
			groupCount++;
		}
	}

	if (!groupCount)
		return;

	result.x /= groupCount;
	result.y /= groupCount;
	result = { result.x - self->center.x, result.y - self->center.y };
	Normalize(result);
}

//***************
// GetGroupSeparation
// flocking utility
// calculates the average separation from group members
// TODO(?): wieghted average
//***************
void GetGroupSeparation(std::vector<std::shared_ptr<GameObject_t>> & areaContents, std::shared_ptr<GameObject_t> & self, Vec2_t & result) {
	result = vec2zero;
	if (!areaContents.size())
		return;

	float groupCount = 0.0f;
	for (auto && entity : areaContents) {
		if (entity->groupID == self->groupID) {
			// FIXME: weigh this based on bounding box size,
			// not just center-to-center range.
			result.x += (entity->center.x - self->center.x);
			result.y += (entity->center.y - self->center.y);
			groupCount++;
		}
	}

	if (!groupCount)
		return;

	result.x /= -groupCount;
	result.y /= -groupCount;
	Normalize(result);
}

//***************
// CheckGroupWaypoint
// flocking utility
// determines if any nearby entity is headed toward the same waypoint as self, 
// and if any of them is close enough to that waypoint, then clear all group-mates' 
// currentWaypoint, hence moving the group onto its next waypoint
// FIXME/BUG: not quite this logic, because if a bunch of entites have crowded the END WAYPOINT
// then the local group will keep jostling to get there (problem? feature?)
//***************
bool CheckGroupWaypoint(std::vector<std::shared_ptr<GameObject_t>> & areaContents, std::shared_ptr<GameObject_t> & self) {

	static std::vector<std::shared_ptr<GameObject_t>> group;

	bool waypointHit = CheckWaypointRange(self);

#if 0
	for (auto && entity : areaContents) {
		if (entity->groupID == self->groupID && !entity->path.empty() && entity->currentWaypoint == self->currentWaypoint) {

			if (!waypointHit) {

				// chache prior group-mates that weren't close enough, but that will need their waypoint cleared
				group.push_back(entity);

				// determine if an entity in the local group is close enough to a shared waypoint
				if (waypointHit = CheckWaypointRange(entity)) {
					for (auto && groupmate : group) {
						groupmate->path.pop_back();
						groupmate->velocity = vec2zero;
						groupmate->currentWaypoint = &groupmate->origin;
					}
				}
			} else {
				entity->path.pop_back();
				entity->velocity = vec2zero;
				entity->currentWaypoint = &entity->origin;
			}
		}
	}
#endif

	if (waypointHit) {
		self->path.pop_back();
		self->velocity = vec2zero;
		self->currentWaypoint = &self->origin;
	}

	group.clear();
	return waypointHit;
}

// END FREEHILL flocking test
///////////////////////////////////////////////////////////////////////////////////////////////

//***************
// TranslateRect
// dynamic pathfinding utility
//***************
SDL_Rect TranslateRect(const SDL_Rect & target, const Vec2_t & v) {
	return SDL_Rect {	(int)nearbyintf(target.x + v.x),
						(int)nearbyintf(target.y + v.y),
						target.w,
						target.h	};
}

//******************
// Rotate
// expanded and factored version of:
// (rotationQuaternion) * quaternionVector( targetX, targetY, 0.0f, 0.0f ) * (rotationQuaternion)^-1
// assumes a right-handed coordinate system
// if clockwise == false, it rotates the target values counter-clockwise
// --only deals with unit length quaternions--
//******************
void Rotate(bool clockwise, float & targetX, float & targetY) {
#define DEG2RAD(angle) ( angle * ((float)(M_PI)/180.0f) )
	static const float ccw[]	= { 0.0f, 0.0f, SDL_sinf(DEG2RAD(1.0f) / 2.0f), SDL_cosf(DEG2RAD(1.0f) / 2.0f) };
	static const float cw[]		= { 0.0f, 0.0f, SDL_sinf(DEG2RAD(-1.0f) / 2.0f), SDL_cosf(DEG2RAD(-1.0f) / 2.0f) };
#undef DEG2RAD

	const float * r;
	if (clockwise)
		r = cw;
	else
		r = ccw;

	float xxzz = r[0]*r[0] - r[2]*r[2];
	float wwyy = r[3]*r[3] - r[1]*r[1];

	float xy2 = r[0]*r[1]*2.0f;
	float zw2 = r[2]*r[3]*2.0f;

	float oldX = targetX;
	float oldY = targetY;

	targetX = (xxzz + wwyy)*oldX + (xy2 - zw2)*oldY;
	targetY = (xy2 + zw2)*oldX + (r[1]*r[1] + r[3]*r[3] - r[0]*r[0] - r[2]*r[2])*oldY;
}

//***************
// CheckOverlap
// dynamic pathfinding utility
//***************
bool CheckOverlap(const SDL_Rect & a, const SDL_Rect & b) {
	int t;
	if ((t = a.x - b.x) > b.w || -t > a.w) return false;
	if ((t = a.y - b.y) > b.h || -t > a.h) return false;
	return true;
}

//***************
// CheckPathCell
// used for dynamic pathfinding
// DEBUG: never call this function with an empty path
//***************
void CheckPathCell(std::shared_ptr<GameObject_t> & entity) {
	if (entity->onPath && !CheckOverlap(entity->bounds, entity->path.back()->bounds)) {	
		entity->onPath = false;
		entity->path.pop_back();										
	}

	if (!entity->path.empty() && CheckOverlap(entity->bounds, entity->path.back()->bounds)) {
		entity->onPath = true;
	}
}

//***************
// RegulateSpeed
// dynamic pathfinding utility
//***************
Vec2_t RegulateSpeed(std::vector<std::shared_ptr<GameObject_t>> & areaContents, std::shared_ptr<GameObject_t> & self) {

	// loop over each local non-self entity testing for collision
	// and determine the minimum distance travellable along the current velocity
	for (float speed = self->speed; speed > 0; speed--) {
		Vec2_t move = self->velocity * speed;
		SDL_Rect testBounds = TranslateRect(self->bounds, move);

		bool collision = false;
		for (auto && entity : areaContents) {
			if (CheckOverlap(testBounds, entity->bounds)) {
				collision = true;
				break;
			}
		}

		if (!collision)
			return move;
	}

	return vec2zero;
}

//***************
// Move
// dynamic pathfinding
//***************
void Move(std::shared_ptr<GameObject_t> & entity) {
	Uint32 dt = SDL_GetTicks() - entity->moveTime;

	if (dt >= 25) {
		entity->moveTime = SDL_GetTicks();

// BEGIN FREEHILL path cell traversal and update test
/*		
			// move along the path from entity to goal to determine if the path needs updating
			// if so, then update the entire path (ie clear and reset it, for now)
			// FIXME/BUG: the path clears out if the endpoint cant be reached...blocked by entities...
			// and updating the goal to the last clear cell along the path only serves to rapidly truncate
			// the path beyond the perimeter of any groups forming near a common ordered goal
			bool pathChanged;
			do {
				pathChanged = false;
				for (auto && cell = entity->path.rbegin(); cell != entity->path.rend(); cell++) {
					// DEBUG: ignore the entity itself as part of the cell contents check
					if (EMPTY_EXCEPT_SELF((**cell), entity)) {
						continue;
					} else {	// re-path to the goal
						pathChanged = PathFind(entity, SDL_Point{ (int)entity->center.x, (int)entity->center.y }, entity->goal);
					}
					if (pathChanged || entity->path.empty())	// DEBUG: changing the path invalidates this loop's iterators
						break;
				}
			} while (pathChanged && !entity->path.empty());
*/
// END FREEHILL path cell traversal and update test

		// determine optimal velocity direction and speed 
		// based on (next waypoint or local velocity gradient) and any possible entities headed to the same point
		std::vector<std::shared_ptr<GameObject_t>> areaContents;
		GetAreaContents((int)(entity->center.x / cellSize), (int)(entity->center.y / cellSize), areaContents, entity);
		
		if (!entity->path.empty()) {	//		if (!CheckGroupWaypoint(areaContents, entity)) {

			// DEBUG: always update velocity based on local gradient and local group alignment,
			// but track directly towards the most recent waypoint if off-path,
			// and once back on path start using the local gradient again
			CheckPathCell(entity);
			if (entity->path.empty()) {
				entity->velocity = vec2zero;
			} else if (entity->onPath && entity->path.size() >= 2) {
				auto & from = entity->path.at(entity->path.size() - 1)->center;
				auto & to = entity->path.at(entity->path.size() - 2)->center;
				Vec2_t localGradient = { (float)(to.x - from.x), (float)(to.y - from.y) };
				Normalize(localGradient);
				entity->velocity = localGradient;
			} else {
				entity->currentWaypoint = &entity->path.back()->center;
				Vec2_t waypointVec = {	(float)(entity->currentWaypoint->x - entity->center.x),
										(float)(entity->currentWaypoint->y - entity->center.y)	};
				Normalize(waypointVec);
				entity->velocity = waypointVec;
			}

												
// BEGIN FREEHILL flocking test
/**/
			// FIXME: dont use specific waypoints because entities wind up jostling for the same waypoint
			// FIXME: these steering forces need to be better balanced so they dont cancel eachother out all the time
			Vec2_t alignment, cohesion, separation;
			GetGroupAlignment(areaContents, entity, alignment);
//			GetGroupCohesion(areaContents, entity, cohesion);
//			GetGroupSeparation(areaContents, entity, separation);
			entity->velocity += alignment;	// separation + alignment + cohesion;
			Normalize(entity->velocity);

// END FREEHILL flocking test

// BEGIN FREEHILL collision sweep test
/*
			// cache references to neighboring cells' contents
			std::vector<SDL_Rect *> areaContents;
			GetAreaContents((int)(entity->centerX / cellSize), (int)(entity->centerY / cellSize), areaContents, entity);

			// check for collision at next step along the static path
			SDL_Rect testBounds = entity->bounds;
			TranslateRect(testBounds, entity->speed * waypointVX, entity->speed * waypointVY);
			bool hit = false;
			SDL_Rect * hitObstacle = nullptr;
			for (auto && obstacle : areaContents) {
				if (SDL_HasIntersection(obstacle, &testBounds)) {
					hitObstacle = obstacle;
					hit = true;
					break;
				}
			}

//			if (hit) {
//				Redirect(entity, areaContents);
//			}


			// there's a dynamic obstacle in the way
			// find a vector that circumnavigates it
			if (hit) {
				// calculate center-to-center non-normalized vector once
				// to set the initial rotation direction
				int dx = (testBounds.x + (testBounds.w / 2)) - (hitObstacle->x + (hitObstacle->w / 2));
				int dy = (testBounds.y + (testBounds.h / 2)) - (hitObstacle->y + (hitObstacle->h / 2));
				bool rotationDir;
				if (waypointVX < waypointVY) {								// entity is heading mostly vertically
					rotationDir = dx < 0 ? CLOCKWISE : COUNTER_CLOCKWISE;	// use x-misalignment
				} else {													// entity is heading mostly horizontally
					rotationDir = dy < 0 ? CLOCKWISE : COUNTER_CLOCKWISE;	// use y-misalignment
				}

				// rotate one way, then the other, from center
				for (int flip = 0; flip < 2; flip++) {
					if (flip) {
						rotationDir = !rotationDir;
						entity->velX = waypointVX;
						entity->velY = waypointVY;
					}

					// set the tangent vector perpendicular to entity's velocity
					// to rotate the testBounds that's one step forward
					// about entity->bounds' center
					hit = false;
					for (int rotationAngle = 1; rotationAngle < 180; rotationAngle++) {	// DEBUG: Rotate(...) uses a 1.0f degree rotation, hence rotationAngle++ and not +=angleIncrement
						testBounds = entity->bounds;
						Rotate(rotationDir, entity->velX, entity->velY);
						TranslateRect(testBounds, entity->speed * entity->velX, entity->speed * entity->velY);

						// check for collision
						for (auto && obstacle : areaContents) {
//							DrawRect(*obstacle, opaqueGreen, false);
//							DrawRect(testBounds, opaqueGreen, true);
//							SDL_RenderPresent(renderer);
							if (SDL_HasIntersection(obstacle, &testBounds)) {
								hit = true;
								break;
							}
						}
						if (!hit)
							break;
					}
					// push a temporary waypoint to track
					if (!hit) {
//						entity->path.push_back(SDL_Point{ testBounds.x + (testBounds.w / 2), testBounds.y + (testBounds.h / 2) });
//						entity->currentWaypoint = &entity->path.back();
						break;
					}
				}
			}

			// no free space in a 180/360 degree forward arc this frame
			if (hit) {
				entity->velX = 0.0f;
				entity->velY = 0.0f;
			}
*/
// END FREEHILL collision sweep test
		} 
		Vec2_t move = RegulateSpeed(areaContents, entity);
		UpdateOrigin(entity, move);
		// if the entity moved, then update gameGrid and internal cell lists for collision filtering
		if (move.x || move.y)
			UpdateCellReferences(entity);

		UpdateBob(entity, move);		// DEBUG: bob does not affect cell location
	}	
}

//***************
// Collide
// collision response called by the sender
// TODO: possibly invoke this during Move
//***************
void Collide(std::shared_ptr<GameObject_t> & sender, std::shared_ptr<GameObject_t> & receiver) {
	//	entities[1]->blinkTime = SDL_GetTicks() + 1000;
	//	entities[1]->damaged = true;
	//	entities[1]->health = -10;
}

//***************
// GoodmanThink
//***************
void GoodmanThink(std::shared_ptr<GameObject_t> & entity) {
	// TODO: Goodman's strategy/state here
	Move(entity);
}

//***************
// MeleeThink
// TODO: master EntityThink() function calls this based on entity type
//***************
void MeleeThink(std::shared_ptr<GameObject_t> & entity) {
	// TODO: resolve standing orders
	Move(entity);
}

//***************
// RangedThink
//***************
void RangedThink(std::shared_ptr<GameObject_t> & entity) {
	// TODO: resolve standing orders
	Move(entity);
}

//***************
// MissileThink
//***************
void MissileThink(std::shared_ptr<GameObject_t> & entity) {
	// TODO: resolve linear trajectory, not pathfinding
	Move(entity);
}

//***************
// Think
// master GameObject_t think function
//***************
void Think() {
	for (auto && entity : entities) {
		switch (entity->type) {
			case OBJECTTYPE_GOODMAN: GoodmanThink(entity);  break;
			case OBJECTTYPE_MELEE: MeleeThink(entity);  break;
			case OBJECTTYPE_RANGED: RangedThink(entity);  break;
			case OBJECTTYPE_MISSILE: MissileThink(entity);  break;
		}
	}
}

//***************
// SelectArea
// monster selection
//***************
void SelectArea(SDL_Point & first, SDL_Point & second) {
	int firstRow	= first.x / cellSize;
	int firstCol	= first.y / cellSize;
	int secondRow	= second.x / cellSize;
	int secondCol	= second.y / cellSize;

	// bounds checking
	if (firstRow < 0)				firstRow = 0;
	else if (firstRow >= gridRows)	firstRow = gridRows - 1;
	if (secondRow < 0)				secondRow = 0;
	else if (secondRow >= gridRows)	secondRow = gridRows - 1;
	if (firstCol < 0)				firstCol = 0;
	else if (firstCol >= gridCols)	firstCol = gridCols - 1;
	if (secondCol < 0)				secondCol = 0;
	else if (secondCol >= gridCols)	secondCol = gridCols - 1;

	// get top-left and bottom-right indexes
	int startRow	= (firstRow < secondRow) ? firstRow : secondRow;
	int endRow		= (startRow == firstRow) ? secondRow : firstRow;
	int startCol	= (firstCol < secondCol) ? firstCol : secondCol;
	int endCol		= (startCol == firstCol) ? secondCol : firstCol;

	// build the list of selected cells to search
	// OR literally just search each cells contents for monsters/goodman
	for (int row = startRow; row <= endRow; row++) {
		for(int col = startCol; col <= endCol; col++)
		selection.push_back(&gameGrid.cells[row][col]);
	}
}

//***************
// ClearAreaSelection
//***************
void ClearAreaSelection() {
	selection.clear();
}

//-------------------------------------END PER-FRAME FUNCTIONS-----------------------------------------//
//-------------------------------------BEGIN MAIN------------------------------------------------------//

//***************
// WinMain
//***************
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow) {
	std::string message;
	// initialization
	if (!InitGame(message)) {
		message += " failed to initialize.";
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Don't Be a Hero!", message.c_str(), NULL);
		SDL_Quit();
		return 0;
	}

	// area select test
	SDL_Point first, second;
	bool beginSelection = false;

	// begin game loop
	int mouseX = -1;
	int mouseY = -1;
	bool running = true;
	SDL_Event event;
	while (running) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT: {
					running = false;
					break;
				}
				case SDL_MOUSEMOTION: {
					mouseX = event.motion.x;
					mouseY = event.motion.y;
					second = { mouseX, mouseY };
					break;
				}
				case SDL_MOUSEBUTTONDOWN: {
					first = { event.button.x, event.button.y };
					beginSelection = true;
					ClearAreaSelection();
					break;
				}
				case SDL_MOUSEBUTTONUP: {
					if (beginSelection) {
						beginSelection = false;
						SelectArea(first, second);
					}

					// test A* pathfinding
					// TODO: call based on a current group/individual selection
					// and loop over that (regardless of orders)
					for (auto && entity : entities) {
						entity->groupID = 1;		// DEBUG: group behavior test (sort of)
						entity->goal = second;
						PathFind(entity, SDL_Point{ (int)entity->center.x, (int)entity->center.y }, second);
					}
					break;
				}
			}
		}

		if (!beginSelection)
			first = second;

		// test basic path following
		Think();

		// begin drawing
		SDL_RenderClear(renderer);

		// draw the map
		SDL_RenderCopy(renderer, map.texture, NULL, &map.frame);

		// draw the grid texture
//		DrawGameGrid();

		// draw the collision layer
		if (DebugCheck(DEBUG_DRAW_COLLISION))
			DrawCollision();

		// draw all entities
		DrawEntities();

		// draw some test text
		DrawOutlineText("Hello There!?\"\'", SDL_Point{mouseX - 32, mouseY - 32}, opaqueGreen);

		// draw a filled rect for cell under the cursor
		int r = mouseX / cellSize;
		int c = mouseY / cellSize;
		if (r >= 0 && r < gridRows && c >= 0 && c < gridCols) {
			SDL_Rect & hover = gameGrid.cells[r][c].bounds;
			DrawRect(hover, transparentGray, true);
		}

		// draw either the selection box
		// or the currently selected cells
		if (beginSelection)
			DrawRect(SDL_Rect{ first.x, first.y, second.x - first.x, second.y - first.y }, opaqueGreen, false);
		else
			DrawSelection();

		SDL_RenderPresent(renderer);
		// end drawing
	}
	// end game loop

	SDL_Quit();
	return 0;
}
//-------------------------------------END MAIN------------------------------------------------------//