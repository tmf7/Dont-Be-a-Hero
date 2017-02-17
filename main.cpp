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

constexpr const Uint32 fps = 30;
constexpr const Uint32 frameTime = 1000 / fps;	// frame-rate governing (rounded from 33.33ms to 33 ms per frame)

// debugging info
typedef enum {
	DEBUG_DRAW_COLLISION	= BIT(0),
	DEBUG_DRAW_PATH			= BIT(1),
	DEBUG_DRAW_OCCUPANCY	= BIT(2)
} DebugFlags_t;

Uint16 debugState =  DEBUG_DRAW_PATH | DEBUG_DRAW_COLLISION;	// DEBUG_DRAW_COLLISION | DEBUG_DRAW_PATH | DEBUG_DRAW_OCCUPANCY;

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

	// negation
	Vec2_s operator-() const {	
		return { -x, -y };
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

	bool operator==(const Vec2_s & rhs) {
		return x == rhs.x && y == rhs.y;
	}

	bool operator!=(const Vec2_s & rhs) {
		return !(*this == rhs);
	}

} Vec2_t;

Vec2_t vec2zero = { 0.0f, 0.0f };

//*****************
// GetAngle
// math helper function
// returns the angle in degrees from the vector components
// DEBUG: assumes the vector is normalized
//*****************
float GetAngle(const Vec2_t & v) {
	float angle = 0.0f;
	if (v.x == 0 && v.y > 0) {
		angle = 90.0f;
	} else if (v.x == 0 && v.y < 0) {
		angle = 270.0f;
	} else {
		float tan = v.y / v.x;
		angle = RAD2DEG(atanf(tan));
		if (v.x < 0)
			angle += 180.0f;
	}
	return angle;
}

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
std::vector<std::shared_ptr<GameObject_t>> groupSelection;	// includes all monsters in interior and border cells of selected area

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

	SDL_RendererFlip	facing;			// left or right to determine flip

	int				health;			// health <= 0 rotates sprite 90 and color-blends gray, hit color-blends red
	int				stamina;		// subtraction color-blends blue for a few frames, stamina <= 0 blinks blue until full
	bool			damaged;
	bool			fatigued;
	Uint32			blinkTime;		// future point to stop color mod

	ObjectType_t	type;			// for faster Think calls
	std::string		name;			// globally unique name (substring can be used for spriteSheet.frameAtlas)
	int				guid;			// globally unique identifier amongst all entites (by number)
	int				groupID;		// selected-group this belongs to
	bool			selected;		// currently controlled monster

	std::vector<GridCell_t *>		path;				// A* pathfinding results
	std::vector<GridCell_t *>		cells;				// currently occupied gameGrid.cells indexes (between 1 and 4)
	bool							onPath;				// if the entity is on the back tile of its path
	SDL_Point *						goal;				// user-defined path objective

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
			facing(SDL_FLIP_NONE),
			blinkTime(0),
			health(0),
			stamina(0),
			damaged(false),
			fatigued(false),
			type(OBJECTTYPE_INVALID),
			guid(-1),
			onPath(false),
			groupID (-1),
			selected(false) {
	};

	GameObject_s(const SDL_Point & origin,  const std::string & name, const int guid, ObjectType_t type) 
		:	origin(origin),
			bounds({ 0, 0, 0, 0 }),
			center(vec2zero),
			velocity(vec2zero),
			name(name),
			bob(0),
			bobMaxed(false),
			moveTime(0),
			facing(SDL_FLIP_NONE),
			blinkTime(0),
			damaged(false),
			fatigued(false),
			type(type),
			guid(guid),
			onPath(false),
			groupID(-1),
			selected(false) {
		switch (type) {
			case OBJECTTYPE_GOODMAN:
				bounds = { origin.x + 4, origin.y + 4, 14, 16 };
				health = 100;
				stamina = 100;
				speed = 4;
				break; 
			case OBJECTTYPE_MELEE:
			case OBJECTTYPE_RANGED: 
				bounds = { origin.x, origin.y + 4, 14, 16 };
				health = 2;
				stamina = -1;
				speed = 2;
				break;
			case OBJECTTYPE_MISSILE: 
				bounds = { origin.x, origin.y, 7, 20 };
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
		center.x = (float)bounds.x + (float)bounds.w / 2.0f;
		center.y = (float)bounds.y + (float)bounds.h / 2.0f;
	};
} GameObject_t;

// entities
std::vector<std::shared_ptr<GameObject_t>> entities;	// monsters and goodman
std::vector<std::shared_ptr<GameObject_t>> missiles;	// kept separate to allow separate spawning and rendering protocols
int entityGUID = 0;

// AreaContents_t
// dynamic pathfinding utility
// DEBUG: convenience typdef for swept AABB collision tests
typedef struct AreaContents_s {
	std::vector<std::shared_ptr<GameObject_t>>	entities;
	std::vector<SDL_Rect *>						obstacles;

	// fills entities and obstacles with the contents of the 9 cells centered at centerPoint
	void Update(const Vec2_t & centerPoint, const std::shared_ptr<GameObject_t> & ignore) {
		static std::vector<int> idList;

		int centerRow = (int)(centerPoint.x / cellSize);
		int centerCol = (int)(centerPoint.y / cellSize);

		for (int row = -1; row <= 1; row++) {
			for (int col = -1; col <= 1; col++) {
				int checkRow = centerRow + row;
				int checkCol = centerCol + col;

				if (checkRow >= 0 && checkRow < gridRows && checkCol >= 0 && checkCol < gridCols) {
					auto & cell = gameGrid.cells[checkRow][checkCol];
					if (!cell.solid) {

						for (auto && entity : cell.contents) {
							if (entity->guid == ignore->guid)
								continue;

							// DEBUG: don't add the same entity twice for those over multiple cells
							auto & checkID = std::find(idList.begin(), idList.end(), entity->guid);
							if (checkID == idList.end()) {
								entities.push_back(entity);
								idList.push_back(entity->guid);
							}
						}
					}
					else {
						obstacles.push_back(&cell.bounds);
					}
				}
			}
		}
		idList.clear();
	}

	// Clear
	void Clear() {
		entities.clear();
		obstacles.clear();
	};
} AreaContents_t;

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

	std::sort(	entities.begin(), 
				entities.end(), 
				[](auto && a, auto && b) { 
					if (a->origin.y < b->origin.y)
						return true;
					else if (a->origin.y == b->origin.y && a->guid < b->guid)	// DEBUG: secondary sort by guid to prevent flicker
						return true;
					return false;
				}	
	);

	// draw all highlights of selected group first
	// TODO: and all other highlights
	for (auto && entity : entities) {

		// draw occpuied cells
		if (DebugCheck(DEBUG_DRAW_OCCUPANCY))
			for (auto && cell : entity->cells)
				DrawRect(cell->bounds, opaqueRed, true);

		if (entity->selected) {
			SDL_Rect & h_srcRect = spriteSheet.frames[spriteSheet.frameAtlas["highlight"]];
			SDL_Rect h_dstRect = { entity->bounds.x - 4, entity->bounds.y - 4, h_srcRect.w + 8, h_srcRect.h + 8 };

			// TODO: set the highlight color  if the entity is headed in for an attack (regardless of group) (make the source white again)

			SDL_RenderCopy(renderer, spriteSheet.texture, &h_srcRect, &h_dstRect);
		}
/*
		// reset texture mods to draw sprite
		SDL_SetTextureColorMod(	spriteSheet.texture,
								spriteSheet.defaultMod.r,
								spriteSheet.defaultMod.g,
								spriteSheet.defaultMod.b	);
		SDL_SetTextureAlphaMod(spriteSheet.texture, spriteSheet.defaultMod.a);
		SDL_SetTextureBlendMode(spriteSheet.texture, SDL_BLENDMODE_BLEND);
*/
	}

	// draw all entities
	for (auto && entity : entities) {

		// get the drawing frames
		int offset = entity->name.find_first_of('_');
		std::string spriteName = entity->name.substr(0, offset);
		SDL_Rect & srcRect = spriteSheet.frames[spriteSheet.frameAtlas[spriteName]];
		SDL_Rect dstRect = { entity->origin.x, entity->origin.y, srcRect.w, srcRect.h };

		// set the left/right flip based on horizontal velocity
		if (entity->velocity.x > 0)
			entity->facing = SDL_FLIP_HORIZONTAL;
		else
			entity->facing = SDL_FLIP_NONE;

		// set color and rotation modifiers
		double angle = 0;
		if (entity->blinkTime > SDL_GetTicks()) {	// override to blink if damaged or fatigued
			const SDL_Color & mod = entity->damaged ? SDL_Color{ 239, 12, 14, SDL_ALPHA_OPAQUE } 
													: (entity->fatigued ? SDL_Color{ 22, 125, 236, SDL_ALPHA_OPAQUE }
																		: spriteSheet.defaultMod);
			SDL_SetTextureColorMod(spriteSheet.texture, mod.r, mod.g, mod.b);
		}
		if (entity->health <= 0) {					// override to rotated gray if dead
			SDL_SetTextureColorMod(spriteSheet.texture, 128, 128, 128);
			angle = entity->facing ? -90 : 90;
		}

		// draw one sprite, then reset modifiers
		SDL_RenderCopyEx(renderer, spriteSheet.texture, &srcRect, &dstRect, angle, NULL, entity->facing);

		// reset texture mods for next sprite
		SDL_SetTextureColorMod(	spriteSheet.texture, 
								spriteSheet.defaultMod.r, 
								spriteSheet.defaultMod.g, 
								spriteSheet.defaultMod.b	);
		SDL_SetTextureAlphaMod(spriteSheet.texture, spriteSheet.defaultMod.a);
		SDL_SetTextureBlendMode(spriteSheet.texture, SDL_BLENDMODE_BLEND);

		// draw collision box
		if (DebugCheck(DEBUG_DRAW_COLLISION))
			DrawRect(entity->bounds, opaqueGreen, false);

		// draw path
		if (DebugCheck(DEBUG_DRAW_PATH))
			DrawPath(entity);
	}


	std::sort(	missiles.begin(), 
				missiles.end(), 
				[](auto && a, auto && b) { 
					if (a->origin.y < b->origin.y)
						return true;
					else if (a->origin.y == b->origin.y && a->guid < b->guid)	// DEBUG: secondary sort by guid to prevent flicker
						return true;
					return false;
				}	
	);

	for (auto && missile : missiles) {
		// rotate the missile along its velocity vector
		// FIXME/BUG: make the missile collision box a small circle/box at the tip
		// UpdateOrigin will have to rotate the BBox around some part of the missile image center
		// depending on the unit-velocity

		SDL_Rect & srcRect = spriteSheet.frames[spriteSheet.frameAtlas["missile"]];
		SDL_Rect dstRect = { missile->origin.x, missile->origin.y, srcRect.w, srcRect.h };
		// angle in degrees
		// FIXME: the sprite starts vertically oriented, so all rotations happend from there... so subtract 90 from the angle?
		float angle = GetAngle(missile->velocity) + 90.0f;
		SDL_RenderCopyEx(renderer, spriteSheet.texture, &srcRect, &dstRect, angle, NULL, SDL_FLIP_NONE);

		// draw collision box
		if (DebugCheck(DEBUG_DRAW_COLLISION))
			DrawRect(missile->bounds, opaqueGreen, false);
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

	SDL_Surface * highlight = IMG_Load("graphics/highlight.png");
	if (!highlight) {
		SDL_FreeSurface(goodman);
		return false;
	}

	SDL_Surface * melee = IMG_Load("graphics/melee.png");
	if (!melee) {
		SDL_FreeSurface(goodman);
		SDL_FreeSurface(highlight);
		return false;
	}

	SDL_Surface * ranged = IMG_Load("graphics/ranged.png");
	if (!ranged) {
		SDL_FreeSurface(goodman);
		SDL_FreeSurface(highlight);
		SDL_FreeSurface(melee);
		return false;
	}

	SDL_Surface * missile = IMG_Load("graphics/missile.png");
	if (!missile) {
		SDL_FreeSurface(goodman);
		SDL_FreeSurface(highlight);
		SDL_FreeSurface(melee);
		SDL_FreeSurface(ranged);
		return false;
	}

	// set width and height of spriteSheet for all sprites in a row
	// DEBUG: hardcoded dimensions determined from manual image file comparision
	int textureWidth = 81;
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
	for (int index = 0; index < 5; index++) {
		SDL_Surface * target;
		std::string name;

		switch (index) {
			case 0: target = goodman; name = "goodman"; break;
			case 1: target = melee; name = "melee"; break;
			case 2: target = ranged; name = "ranged"; break;
			case 3: target = missile; name = "missile"; break;
			case 4: target = highlight; name = "highlight"; break;
			default: target = NULL; name = "invalid"; break;
		}
		frame = { origin.x, origin.y, target->w, target->h };
		if (SDL_UpdateTexture(spriteSheet.texture, &frame, target->pixels, target->pitch)) {
			SDL_FreeSurface(goodman);
			SDL_FreeSurface(highlight);
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
	SDL_FreeSurface(highlight);
	SDL_FreeSurface(melee);
	SDL_FreeSurface(ranged);
	SDL_FreeSurface(missile);

	// cache the original color and alpha mod
	SDL_GetTextureColorMod(	spriteSheet.texture, 
							&spriteSheet.defaultMod.r, 
							&spriteSheet.defaultMod.g, 
							&spriteSheet.defaultMod.b	);
	SDL_GetTextureAlphaMod(spriteSheet.texture, &spriteSheet.defaultMod.a);
	return true;
}

//***************
// ClearCellReferences
// collision utitliy
//***************
void ClearCellReferences(std::shared_ptr<GameObject_t> & entity) {
	// remove the entity from any gameGrid.cells its currently in
	for (auto && cell : entity->cells) {
		auto & index = std::find(cell->contents.begin(), cell->contents.end(), entity);
		if (index != cell->contents.end())
			cell->contents.erase(index);
	}

	// empty the entity's cell references
	entity->cells.clear();
}

//***************
// UpdateCellReferences
// collision utitliy
//***************
void UpdateCellReferences(std::shared_ptr<GameObject_t> & entity) {
	ClearCellReferences(entity);

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
// GetSpawnPoint
//***************
SDL_Point GetSpawnPoint(const ObjectType_t type) {
	SDL_Point spawnPoint;
	bool invalidSpawnPoint = false;
	do {
		spawnPoint.x = rand() % gameWidth;
		spawnPoint.y = rand() % (gameHeight - cellSize);	// DEBUG: no grid row along bottom of the screen
		invalidSpawnPoint = false;

		// check the spawnPoint's resulting collision bounds' four corners,
		// which will be over 1 - 4 gameGrid cells, doesn't overlap another entity, a solid cell, and is on the map
		SDL_Rect collideBounds; 
		switch (type) {
			case OBJECTTYPE_GOODMAN: collideBounds = { spawnPoint.x + 4, spawnPoint.y + 4, 14, 16 }; break;
			case OBJECTTYPE_MELEE:
			case OBJECTTYPE_RANGED: collideBounds = { spawnPoint.x, spawnPoint.y + 4, 14, 16 }; break;
			case OBJECTTYPE_MISSILE: collideBounds = { spawnPoint.x + 4, spawnPoint.y + 4, 8, 16 };  break;
			default: collideBounds = { spawnPoint.x, spawnPoint.y + 4, 14, 16 }; break;
		}

		for (int corner = 0; corner < 4; corner++) {
			SDL_Point testPoint;
			switch (corner) {
			case 0: testPoint = { collideBounds.x , collideBounds.y }; break;
			case 1: testPoint = { collideBounds.x + collideBounds.w, collideBounds.y }; break;
			case 2: testPoint = { collideBounds.x , collideBounds.y + collideBounds.h }; break;
			case 3: testPoint = { collideBounds.x + collideBounds.w , collideBounds.y + collideBounds.h }; break;
			}

			int testRow = testPoint.x / cellSize;
			int testCol = testPoint.y / cellSize;
			if (testRow < 0 || testRow >= gridRows || testCol < 0 || testCol >= gridCols) {
				invalidSpawnPoint = true;
				break;
			}

			GridCell_t & cell = gameGrid.cells[testRow][testCol];
			if (cell.solid || !cell.contents.empty()) {
				invalidSpawnPoint = true;
				break;
			}
		}
	} while (invalidSpawnPoint);
	return spawnPoint;
}

//***************
// SpawnGoodman
//***************
void SpawnGoodman() {
	SDL_Point spawnPoint = GetSpawnPoint(OBJECTTYPE_GOODMAN);
	std::string name = "goodman";
	std::shared_ptr<GameObject_t> goodman = std::make_shared<GameObject_t>(spawnPoint, name, entityGUID, OBJECTTYPE_GOODMAN);
	entities.push_back(goodman);
	UpdateCellReferences(goodman);
	entityGUID++;
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
	for (int count = 0; count < 10; count++) {
		
		// give it a type and name
		ObjectType_t type = (ObjectType_t)((rand() % 2) + 1);
		std::string name;
		switch (type) {
			case OBJECTTYPE_MELEE: name = "melee_"; break;
			case OBJECTTYPE_RANGED: name = "ranged_"; break;
		}
		name += std::to_string(entityGUID);

		// get an valid spawnpoint
		SDL_Point spawnPoint = GetSpawnPoint(type);

		// add it to the entity vector and gameGrid
		std::shared_ptr<GameObject_t> monster = std::make_shared<GameObject_t>(spawnPoint, name, entityGUID, type);
		entities.push_back(monster);
		UpdateCellReferences(monster);
		entityGUID++;
	}
}

//***************
// SpawnMissile
// TODO: give GameObject_t an owner variable, for most monsters/goodman its themselves, 
// but for missiles its the attacker so goodman knows who attacked
// TODO: perhaps for sophisticated monster grouping the owner could be the group leader
//***************
void SpawnMissile(const Vec2_t & origin, const Vec2_t & direction) {
	SDL_Point spawnPoint = { (int)(origin.x), (int)(origin.y) };
	// FIXME: situate the (image top-left) spawnPoint such that the given origin coincides with the...resulting bounds' center
	// missile bounds (currently) { origin.x + 4, origin.y + 4, 8, 16 };
	// missile image dimensions: 7w x 20h 

	std::string name = "missile_" + std::to_string(entityGUID);
	std::shared_ptr<GameObject_t> missile = std::make_shared<GameObject_t>(spawnPoint, name, entityGUID, OBJECTTYPE_MISSILE);
	missiles.push_back(missile);
	UpdateCellReferences(missile);
	missile->velocity = direction;
	entityGUID++;
}

//***************
// RemoveEntity
//***************
void RemoveEntity(std::shared_ptr<GameObject_t> & entity) {
	ClearCellReferences(entity);

	// DEBUG: test removal from groupSelection vector first to be sure even if it wasn't selected
	// that the memory block is still in use to be tested (instead of getting a read-access error)
	if (entity->selected) {
		auto & index = std::find(groupSelection.begin(), groupSelection.end(), entity);
		groupSelection.erase(index);
	}
	
	if (entity->type == OBJECTTYPE_MISSILE) {
		auto & index = std::find(missiles.begin(), missiles.end(), entity);
		missiles.erase(index);
	} else {
		auto & index = std::find(entities.begin(), entities.end(), entity);
		entities.erase(index);
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
// PointToCell
// converts 2D point to valid grid cell indexes
//***************
void PointToCell(const SDL_Point & point, int & row, int & col) {
	row = point.x / cellSize;
	col = point.y / cellSize;

	// snap off-map row
	if (row < 0)
		row = 0;
	else if (row >= gridRows)
		row = gridRows - 1;

	// snap off-map row
	if (col < 0)
		col = 0;
	else if (col >= gridCols)
		col = gridCols - 1;
}

//***************
// PointToCell
// returns a validated grid cell 
// under the given point
//***************
GridCell_t & PointToCell(const SDL_Point & point) {
	int row, col;
	PointToCell(point, row, col);
	return gameGrid.cells[row][col];
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

	int startRow;
	int startCol;
	int endRow;
	int endCol;
	PointToCell(start, startRow, startCol);
	PointToCell(goal, endRow, endCol);

	GridCell_t * startCell = &gameGrid.cells[startRow][startCol];
	GridCell_t * endCell = &gameGrid.cells[endRow][endCol];

	if (endCell->solid || startCell == endCell) {
		entity->path.clear();
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

			// the path starts on the entity's current cell
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
	return false;		// DEBUG: this will be hit if an entity is surrounded by entities on its first search
}

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
// DEBUG: never call with entity->path.empty()
//***************
bool CheckWaypointRange(std::shared_ptr<GameObject_t> & entity) {
	int xRange = SDL_abs((int)(entity->center.x - entity->path.back()->center.x));
	int yRange = SDL_abs((int)(entity->center.y - entity->path.back()->center.y));
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
void UpdateOrigin(std::shared_ptr<GameObject_t>  & entity, const Vec2_t & move) {
	int dx = (int)nearbyintf(move.x);
	int dy = (int)nearbyintf(move.y);

	entity->origin.x += dx;
	entity->origin.y += dy;
	entity->bounds.x += dx;
	entity->bounds.y += dy;
	entity->center.x += nearbyintf(move.x);
	entity->center.y += nearbyintf(move.y);
	int derp = 0;
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

// END FREEHILL flocking test
///////////////////////////////////////////////////////////////////////////////////////////////

//******************
// Rotate
// expanded and factored version of:
// (rotationQuaternion) * quaternionVector( targetX, targetY, 0.0f, 0.0f ) * (rotationQuaternion)^-1
// assumes a right-handed coordinate system
// if clockwise == false, it rotates the target values counter-clockwise
// --only deals with unit length quaternions--
//******************
void Rotate(bool clockwise, Vec2_t & result) {
	static const float ccw[]	= { 0.0f, 0.0f, SDL_sinf(DEG2RAD(1.0f) / 2.0f), SDL_cosf(DEG2RAD(1.0f) / 2.0f) };
	static const float cw[]		= { 0.0f, 0.0f, SDL_sinf(DEG2RAD(-1.0f) / 2.0f), SDL_cosf(DEG2RAD(-1.0f) / 2.0f) };

	const float * r;
	if (clockwise)
		r = cw;
	else
		r = ccw;

	float xxzz = r[0]*r[0] - r[2]*r[2];
	float wwyy = r[3]*r[3] - r[1]*r[1];

	float xy2 = r[0]*r[1]*2.0f;
	float zw2 = r[2]*r[3]*2.0f;

	float oldX = result.x;
	float oldY = result.y;

	result = {	(xxzz + wwyy)*oldX + (xy2 - zw2)*oldY,
				(xy2 + zw2)*oldX + (r[1]*r[1] + r[3]*r[3] - r[0]*r[0] - r[2]*r[2])*oldY };
}

//***************
// Maximize
//***************
float Maximize(const float & a, const float & b) {
	return a > b ? a : b;
}

//***************
// Minimize
//***************
float Minimize(const float & a, const float & b) {
	return a < b ? a : b;
}

//***************
// GetSurfaceNormal
// DEBUG: disregards collision
//***************
Vec2_t GetSurfaceNormal(const SDL_Rect & a, const SDL_Rect & b, const Vec2_t & va, const Vec2_t & vb) {
	Vec2_t normal = vec2zero;
	Vec2_t aMin = { (float)a.x, (float)a.y };
	Vec2_t aMax = { (float)(a.x + a.w), (float)(a.y + a.h) };
	Vec2_t bMin = { (float)b.x, (float)b.y };
	Vec2_t bMax = { (float)(b.x + b.w), (float)(b.y + b.h) };
	Vec2_t relativeV = va - vb;

	for (int i = 0; i < 2; i++) {
		if (relativeV[i] <= 0.0f) {
//			if (aMax[i] < bMin[i]) /* does nothing, yet */;
			if (bMax[i] <= aMin[i]) normal[i] = 1.0f;
//			if (aMax[i] > bMin[i])	/* does nothing, yet */;
		}
		if (relativeV[i] > 0.0f) {
//			if (aMin[i] > bMax[i]) /* does nothing, yet */;
			if (aMax[i] <= bMin[i]) normal[i] = -1.0f;
//			if (bMax[i] > aMin[i]) /* does nothing, yet */;
		}
	}
	return normal;
}

//***************
// TranslateRect
// dynamic pathfinding utility
//***************
SDL_Rect TranslateRect(const SDL_Rect & src, const Vec2_t translation) {
	return SDL_Rect {	(int)nearbyintf(src.x + translation.x), 
						(int)nearbyintf(src.y + translation.y), 
						src.w, 
						src.h };
}


//***************
// BroadPhaseAABB
// dynamic pathfinding utility
// Broad-phase moving AABB-AABB collision test utility
//***************
SDL_Rect GetBroadPhaseAABB(std::shared_ptr<GameObject_t> & entity) {
	SDL_Rect bpAABB;
	Vec2_t sweep = entity->velocity * entity->speed;
	bpAABB.x = entity->velocity.x > 0.0f ? entity->bounds.x : (int)nearbyintf(entity->bounds.x + sweep.x);
	bpAABB.y = entity->velocity.y > 0.0f ? entity->bounds.y : (int)nearbyintf(entity->bounds.y + sweep.y);
	bpAABB.w = entity->velocity.x > 0.0f ? (int)nearbyintf(entity->bounds.w + sweep.x) : (int)nearbyintf(entity->bounds.w - sweep.x);
	bpAABB.h = entity->velocity.y > 0.0f ? (int)nearbyintf(entity->bounds.h + sweep.y) : (int)nearbyintf(entity->bounds.h - sweep.y);
	return bpAABB;
}

//***************
// AABBAABBTest
// dynamic pathfinding utility
// AABB-AABB collision test
// DEBUG: includes touching
// returns true in the case of intersection
//***************
bool AABBAABBTest(const SDL_Rect & a, const SDL_Rect & b) {
	int t;
	// if((t = a.x - b.x) == b.w || -t == a.w) return true; (confirms touching in x)
	// if((t = a.y - b.y) == b.h || -t == a.h) return true; (confirms touching in y)

	if ((t = a.x - b.x) > b.w || -t > a.w) return false;
	if ((t = a.y - b.y) > b.h || -t > a.h) return false;
	return true;
}

//***************
// MovingAABBAABBTest
// dynamic pathfinding utility
// AABB-AABB collision test where both are moving
// returns true if already in collision
// returns false if no collision will occur
// otherwise returns the fraction along the movement
// where collision first occurs
//***************
bool MovingAABBAABBTest(const SDL_Rect & a, const SDL_Rect & b, const Vec2_t & va, const Vec2_t & vb, Vec2_t & times) {

	// started in collision
	if (AABBAABBTest(a, b)) {
		times = vec2zero;
		return true;
	}

	// FIXME/BUG: the collision normal is not computed if the entity walked exactly up to the obstacle as to touch it this frame
	// then NEXT frame its right on it, so NONE of the if conditions execute

	Vec2_t aMin = { (float)a.x, (float)a.y };
	Vec2_t aMax = { (float)(a.x + a.w), (float)(a.y + a.h) };
	Vec2_t bMin = { (float)b.x, (float)b.y };
	Vec2_t bMax = { (float)(b.x + b.w), (float)(b.y + b.h) };
	Vec2_t relativeV = va;// -vb;		// FIXME/DEBUG: fully kinematic simulation with non-simultaneous motion doesn't need relative velocity
	times.x = 0.0f;	// DEBUG: tFirst
	times.y = 1.0f;	// DEBUG: tLast

	// determine times of first and last contact, if any
	for (int i = 0; i < 2; i++) {
		if (relativeV[i] < 0.0f) {
			if (aMax[i] < bMin[i]) return false; // non-intersecting and moving apart
			if (bMax[i] < aMin[i]) times.x = Maximize((bMax[i] - aMin[i]) / relativeV[i], times.x);
			if (aMax[i] > bMin[i]) times.y = Minimize((bMin[i] - aMax[i]) / relativeV[i], times.y);
		}
		if (relativeV[i] > 0.0f) {
			if (aMin[i] > bMax[i]) return false; // non-intersecting and moving apart
			if (aMax[i] < bMin[i]) times.x = Maximize((bMin[i] - aMax[i]) / relativeV[i], times.x);
			if (bMax[i] > aMin[i]) times.y = Minimize((bMax[i] - aMin[i]) / relativeV[i], times.y);
		}
		
		// generally, too far away to make contact
		// DEBUG: if tFirst == tLast == 1.0f then this function returns true (a collision will occur)
		// however, tFirst can also wind up as 0.0f but this function returns false, so tFirst ISN'T the final decider of collision
		if (times.x > times.y)
			return false;
	}
	return true;
}

//***************
// CheckPathCell
// used for dynamic pathfinding
// DEBUG: never call this function with an empty path
//***************
void CheckPathCell(std::shared_ptr<GameObject_t> & entity) {
	if (entity->onPath && !AABBAABBTest(entity->bounds, entity->path.back()->bounds)) {	
		entity->onPath = false;
		entity->path.pop_back();										
	}

	if (!entity->path.empty() && AABBAABBTest(entity->bounds, entity->path.back()->bounds)) {
		entity->onPath = true;
	}
}

//***************
// CheckForwardCollision
// returns the fraction along the current velocity where
// touching first occurs between self and 
// local entities or static obstacles, if any
// DEBUG: never returns 0.0f which implies two things started in overlap/touching
// sets the collision entity, if any
// otherwise sets the collision entity to nullptr
//***************
float CheckForwardCollision(std::shared_ptr<GameObject_t> & self, const AreaContents_t & contents, std::shared_ptr<GameObject_t> & collisionEntity) {

	float nearest = 1.0f;
	const Vec2_t selfMove = self->velocity * self->speed;
	SDL_Rect broadPhaseBounds = GetBroadPhaseAABB(self);
	SDL_Rect nextSelfBounds = TranslateRect(self->bounds, selfMove);

	// entity check
	collisionEntity.reset();
	for (auto && entity : contents.entities) {

		// broad-phase test first,
		// then so a swept AABB test on the next n steps
		Vec2_t times;
		if (AABBAABBTest(broadPhaseBounds, entity->bounds) &&
			MovingAABBAABBTest(nextSelfBounds, entity->bounds, selfMove, entity->velocity * entity->speed, times)) {

			// DEBUG: don't even consider a move along a vector with ANY collision
			collisionEntity = entity;
			nearest = 0.0f;
			break;
		}
	}

	// static obstacle check
	for (auto && obstacle : contents.obstacles) {

		// broad-phase test first
		// then so a swept AABB test on the next n steps
		Vec2_t times;
		if (AABBAABBTest(broadPhaseBounds, *obstacle) && 
			MovingAABBAABBTest(nextSelfBounds, *obstacle, selfMove, { 0.0f, 0.0f }, times)) {
				collisionEntity.reset();
				nearest = 0.0f;
				break;
		}
	}
	return nearest;
}

//***************
// AvoidCollision
// dynamic pathfinding utility
// updates current velocity to avoid predicted collisions
// with all area contents (dynamic and static obstacles)
// returns the maximum fraction along any forward velocity
// before that avoids all collision
//***************
float AvoidCollision(std::shared_ptr<GameObject_t> & self, const AreaContents_t & contents) {
	static std::shared_ptr<GameObject_t> collisionEntity;

	Vec2_t desiredVelocity = self->velocity;

	// rotated 90 degrees CCW to setup for the 180 degree CW sweep
	self->velocity = { -self->velocity.y, self->velocity.x };

	float bestWeight = 0.0f;
	float bestFraction = 0.0f;
	Vec2_t bestVelocity = vec2zero;

	// check a 180 degree forward arc maximizing movement along path
	for (int angle = 0; angle < 360; angle++) {
		Rotate(CLOCKWISE, self->velocity);
		float fraction = CheckForwardCollision(self, contents, collisionEntity);	// DEBUG: forced to either 0.0f or 1.0f
		float weight = (self->velocity * desiredVelocity);

		if (fraction > bestFraction || (fraction == bestFraction && weight > bestWeight)) {
			bestWeight = weight;
			bestFraction = fraction;
			bestVelocity = self->velocity;
		}
	}
	self->velocity = bestVelocity;
	return bestFraction;
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
// Walk
// dynamic pathfinding
//***************
void Walk(std::shared_ptr<GameObject_t> & entity) {
	static AreaContents_t areaContents;						// fetched and used once per frame per entity, then cleared

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

		// determine optimal unit-velocity and speed 
		Vec2_t move = vec2zero;

		if (!entity->path.empty()) {

			// head towards last waypoint if off-path,
			// otherwise use the local gradient
			CheckPathCell(entity);
			if (entity->onPath && entity->path.size() >= 2) {
				auto & from = entity->path.at(entity->path.size() - 1)->center;
				auto & to = entity->path.at(entity->path.size() - 2)->center;
				Vec2_t localGradient = { (float)(to.x - from.x), (float)(to.y - from.y) };
				Normalize(localGradient);
				entity->velocity = localGradient;
			} else if (!entity->path.empty()) {	// !entity->onPath || entity->path.size() < 2
				// FIXME/BUG: check if the path is empty again here
				// ...or change CheckPathCell to not pop_back a waypoint
				auto & currentWaypoint = entity->path.back()->center;
				if (!CheckWaypointRange(entity)) {
					Vec2_t waypointVec = {	(float)(currentWaypoint.x - entity->center.x),
											(float)(currentWaypoint.y - entity->center.y)	};
					Normalize(waypointVec);
					entity->velocity = waypointVec;
				} else {
					entity->path.pop_back();
					entity->velocity = vec2zero;
				}
			}
									
// BEGIN FREEHILL flocking test
/*
			// FIXME: dont use specific waypoints because entities wind up jostling for the same waypoint
			// FIXME: these steering forces need to be better balanced so they dont cancel eachother out all the time
			// FIXME: using this with gradient velocities throws entities off the end of the trail infinitely (until given a new path)
			Vec2_t alignment, cohesion, separation;
			GetGroupAlignment(areaEntities, entity, alignment);
			GetGroupCohesion(areaEntities, entity, cohesion);
			GetGroupSeparation(areaEntities, entity, separation);
			entity->velocity +=  separation + alignment + cohesion;
			Normalize(entity->velocity);
*/
// END FREEHILL flocking test

			// stop moving if the path is crowded
			areaContents.Update(entity->center, entity);
			std::shared_ptr<GameObject_t> collisionEntity;
			float fraction = CheckForwardCollision(entity, areaContents, collisionEntity);
			if (fraction < 1.0f && collisionEntity) {
				if (collisionEntity->velocity == vec2zero) {
					// FIXME: occasional perma-bob vibrator due to clear path but obstructed velocity
					// (collision check doesn't quite fix this)
					bool pathCrowded = std::find_if(	entity->path.begin(), 
														entity->path.end(),
														[&entity](auto && cell) { 
															return EMPTY_EXCEPT_SELF((*cell), entity);	// cell->contents.empty();
													}) == entity->path.end();
					if (pathCrowded) { 
						entity->velocity = vec2zero;
					}
				}
			}

			// pick a different trajectory if there's no way forward
			// FIXME/BUG: because fraction reported is always > 0.0f the entity will always move
			// which is probably why the entity walks straight into others (totally overlapping)
			// FIXME: AvoidCollision() sets the velocity regardless of the fraction
			// however fraction SHOULD stop it if its 0.0f (note: not the main issue)
			if (entity->velocity != vec2zero)
				fraction = AvoidCollision(entity, areaContents);
			move = entity->velocity * entity->speed * fraction;// *((float)frameTime / 1000.0f);
			UpdateOrigin(entity, move);
		}

		// if the entity moved, then update gameGrid and internal cell lists for collision filtering
		if (move.x || move.y)
			UpdateCellReferences(entity);

		UpdateBob(entity, move);		// DEBUG: bob does not affect cell location
	}
	areaContents.Clear();
}

//***************
// Fly
// linear movement
//***************
void Fly(std::shared_ptr<GameObject_t> & entity) {
	Uint32 dt = SDL_GetTicks() - entity->moveTime;

	if (dt >= 25) {
		entity->moveTime = SDL_GetTicks();
		Vec2_t move = entity->velocity * entity->speed;
		UpdateOrigin(entity, move);
		UpdateCellReferences(entity);
		UpdateBob(entity, move);		// DEBUG: bob does not affect cell location
	}
}

//***************
// GoodmanThink
//***************
void GoodmanThink(std::shared_ptr<GameObject_t> & entity) {
	// TODO: Goodman's strategy/state here
	Walk(entity);
}

//***************
// MeleeThink
// TODO: master EntityThink() function calls this based on entity type
//***************
void MeleeThink(std::shared_ptr<GameObject_t> & entity) {
	// TODO: resolve standing orders
	Walk(entity);
}

//***************
// RangedThink
//***************
void RangedThink(std::shared_ptr<GameObject_t> & entity) {
	// TODO: resolve standing orders
	Walk(entity);
}

//***************
// MissileThink
//***************
void MissileThink(std::shared_ptr<GameObject_t> & entity) {
	Fly(entity);
//	if (CheckLocalCollision(entity, nullptr)) {
		// TODO: only do damage to goodman
		// TODO: explode no matter  (begin explosion animation and sound)
	//	RemoveEntity(entity);
//	}
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
		}
	}

	for (auto && missile : missiles)
		MissileThink(missile);
}

//***************
// SelectGroup
// monster selection
//***************
void SelectGroup(SDL_Point & first, SDL_Point & second) {

	static std::vector<int> idList;

	int firstRow;
	int firstCol;
	int secondRow;
	int secondCol;
	PointToCell(first, firstRow, firstCol);
	PointToCell(second, secondRow, secondCol);

	// get top-left and bottom-right indexes
	int startRow	= (firstRow < secondRow) ? firstRow : secondRow;
	int endRow		= (startRow == firstRow) ? secondRow : firstRow;
	int startCol	= (firstCol < secondCol) ? firstCol : secondCol;
	int endCol		= (startCol == firstCol) ? secondCol : firstCol;

	// add all monsters in the selected area to a group
	for (int row = startRow; row <= endRow; row++) {
		for (int col = startCol; col <= endCol; col++) {
			auto & target = gameGrid.cells[row][col];
			if (!target.solid) {

				for (auto && entity : target.contents) {
					if (entity->health <= 0)
						continue;
					if (entity->type != OBJECTTYPE_MELEE && entity->type != OBJECTTYPE_RANGED)
						continue;

					// DEBUG: don't add the same entity twice for those over multiple cells
					auto & checkID = std::find(idList.begin(), idList.end(), entity->guid);
					if (checkID == idList.end()) {
						entity->selected = true;
						entity->groupID = 4;		// TODO: random group number for now, but use available/forced group number tracking
						groupSelection.push_back(entity);
						idList.push_back(entity->guid);
					}
				}
			}
		}
	}
	idList.clear();
}

//***************
// ClearGroupSelection
//***************
void ClearGroupSelection() {
	for (auto && entity : groupSelection) {
		entity->groupID = -1;
		entity->selected = false;
	}
	groupSelection.clear();
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
	int mouseX = -1;
	int mouseY = -1;

	// begin game loop
	bool running = true;
	SDL_Event event;
	while (running) {

		// cpu-independent interval (eg: for smooth movement)
		Uint32 startTime = SDL_GetTicks();

		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT: {
					running = false;
					break;
				}
				case SDL_KEYDOWN: {
					if (event.key.keysym.scancode == SDL_SCANCODE_SPACE)
						ClearGroupSelection();
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
					if (groupSelection.empty()) {
						beginSelection = true;
					} 
					break;
				}
				case SDL_MOUSEBUTTONUP: {
					if (beginSelection) {
						beginSelection = false;
						SelectGroup(first, second);
					} else if (!groupSelection.empty()) {

						// check for an attack on goodman
						auto & cell = PointToCell(second);
						auto & findGoodman = std::find_if(	cell.contents.begin(),
															cell.contents.end(),
															[](auto && entity) {
															return entity->type == OBJECTTYPE_GOODMAN;
														});
						if (findGoodman != cell.contents.end()) {
							// launch a missle attack from all ranged monsters in the group
							for (auto && entity : groupSelection) {
								if (entity->type == OBJECTTYPE_RANGED) {
									Vec2_t launchDir = (*findGoodman)->center - entity->center;
									Normalize(launchDir);
									SpawnMissile(entity->center, launchDir);
								}
							}

							// TODO: set all group's monster's goal to goodman's center and continually pathfind to him each frame
							// TODO: have MELEE types chase and hit him (then back away quickly?)
						}
						
						// TODO: alternatively if shift is held for down and up click, launch a
						// stationary attack towards the current mouse location (second point)

						else {
							// group A* pathfinding
							// DEBUG: only control one group at a time
							// TODO: quickly label/re-label and toggle between groups
							for (auto && entity : groupSelection) {
								entity->goal = &cell.center;
								PathFind(entity, SDL_Point{ (int)entity->center.x, (int)entity->center.y }, second);
							}
						}
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

		// draw the selection box
		if (beginSelection)
			DrawRect(SDL_Rect{ first.x, first.y, second.x - first.x, second.y - first.y }, opaqueGreen, false);

		SDL_RenderPresent(renderer);
		// end drawing

		// frame-rate governing delay
		Uint32 deltaTime = SDL_GetTicks() - startTime;

		// DEBUG: breakpoint handling
		if (deltaTime > 1000)
			deltaTime = frameTime;

		// DEBUG: delta time of this last frame is not used as the global update interval,
		// instead the globally available update interval is fixed to frameTime
		deltaTime <= frameTime ? SDL_Delay(frameTime - deltaTime) : SDL_Delay(deltaTime - frameTime);
	}
	// end game loop

	SDL_Quit();
	return 0;
}
//-------------------------------------END MAIN------------------------------------------------------//