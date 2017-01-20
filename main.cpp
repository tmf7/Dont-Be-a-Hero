//**********************************************
//*
//* Copyright Thomas Freehill January 14 2017
//* Don't Be a Hero (game)
//* 
//**********************************************

#include <Windows.h>
#include "Definitions.h"

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

Uint16 debugState = DEBUG_DRAW_PATH;// DEBUG_DRAW_COLLISION | DEBUG_DRAW_PATH;

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

// forward declaration of GameObject_t
typedef struct GameObject_s GameObject_t;

// GridCell_t
typedef struct GridCell_s {
	// pathfinding
	GridCell_s * parent;		// originating cell to set the path back from the goal
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
	SDL_Point	origin;			// top-left of sprite image
	SDL_Rect	bounds;			// world-position and size of collision box
	int			speed;
	float		velX;
	float		velY;
	float		centerX;		// center of the collision bounding box
	float		centerY;		// center of the collision bounding box

	int			bob;			// the illusion of walking
	bool		bobMaxed;		// peak bob height, return to 0
	Uint32		moveTime;		// delay between move updates

	int			facing;			// left or right to determine flip
	int			health;			// health <= 0 rotates sprite 90 and color-blends gray, hit color-blends red
	int			stamina;		// subtraction color-blends blue for a few frames, stamina <= 0 blinks blue until full
	bool		damaged;
	bool		fatigued;
	Uint32		blinkTime;		// future point to stop color mod

	ObjectType_t	type;		// for faster Think calls
	std::string		name;		// globally unique name (substring can be used for spriteSheet.frameAtlas)

	SDL_Point *						currentWaypoint;
	std::vector<GridCell_t *>		path;				// A* pathfinding results
	

	std::vector<GridCell_t *>	cells;	// currently occupied gameGrid.cells indexes (between 1 and 4)

	GameObject_s() 
		:	origin({0, 0}),
			bounds({0, 0, 0, 0}),
			centerX(0.0f),
			centerY(0.0f),
			speed(0),
			velX(0),
			velY(0),
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
			type(OBJECTTYPE_INVALID) {
			currentWaypoint = &origin;
	};

	GameObject_s(const SDL_Point & origin,  const std::string & name, ObjectType_t type) 
		:	origin(origin),
			velX(0),
			velY(0),
			name(name),
			bob(0),
			bobMaxed(false),
			moveTime(0),
			facing(rand() % 2),
			blinkTime(0),
			damaged(false),
			fatigued(false),
			type(type) {
		currentWaypoint = &(this->origin);
		bounds = {origin.x + 4, origin.y + 4, 8, 16 };
		centerX = (float)bounds.x + (float)bounds.w / 2.0f;
		centerY = (float)bounds.y + (float)bounds.h / 2.0f;
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
	for (auto && cell : entity->path)
		DrawRect(SDL_Rect{ cell->center.x, cell->center.y, 2, 2 }, opaqueGreen, true);
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
	std::shared_ptr<GameObject_t> goodman = std::make_shared<GameObject_t>(SDL_Point{ spawnX, spawnY }, name, OBJECTTYPE_GOODMAN);
	entities.push_back(goodman);
	entityAtlas[name] = entityGUID++;
	UpdateCellReferences(goodman);
}

//***************
// SpawnMonsters
// TODO: cluster spawn points in a region of the map farthest from Goodman
// OR: potentially pick TWO  spawn points, then "animate" the monsters flooding in (which affects strategy)
// TODO: make sure to update grid and monsters/goodman cell lists when spawning
//***************
void SpawnMonsters() {
	// seed the random number generator
	srand(SDL_GetTicks());

	// a valid spawnpoint is not solid 
	// and has no other gameobjects
	for (int count = 0; count < 2; count++) {
		
		SDL_Point spawnPoint;
		SDL_Point spawnCell;
		do {
			spawnPoint.x = rand() % gameWidth;					
			spawnPoint.y = rand() % (gameHeight - cellSize);	// DEBUG: no grid row along bottom of the screen
			spawnCell.x = spawnPoint.x / cellSize;				// FIXME(?): bounds check these row/col
			spawnCell.y = spawnPoint.y / cellSize;
		} while (	gameGrid.cells[spawnCell.x][spawnCell.y].solid ||
					!gameGrid.cells[spawnCell.x][spawnCell.y].contents.empty()	);

		// give it a type and name
		ObjectType_t type = (ObjectType_t)((rand() % 2) + 1);
		std::string name;
		switch (type) {
			case OBJECTTYPE_MELEE: name = "melee_"; break;
			case OBJECTTYPE_RANGED: name = "ranged_"; break;
		}
		name += std::to_string(entityGUID);

		// add it to the entity vector, entityAtlas, and gameGrid
		std::shared_ptr<GameObject_t> monster = std::make_shared<GameObject_t>(spawnPoint, name, type);
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
// PathFind
// A* search of gameGrid cells
// only searches static non-solid geometry
// entities will perform dynamic collision avoidance on the fly
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
////////////////////////////////////////////////////////////////////////////////////////////////////////

	// if the endCell is obstructed by a dynamic obstacle, 
	// then floodfill for a nearby empty cell to set as the goal
	if (!endCell->contents.empty() && !(endCell->contents.size() == 1 && endCell->contents[0] == entity)) {

		// continue until there are no new neighbors to search
		// DEBUG: this simple conditions works because my hard-coded map is all open areas
		// DEBUG: however, there is an alternative where one searches one of several vectors
		// containing cells paired according to color-codes indicating their pre-processed static reachability
		closedSet.clear();
		closedSet.push_back(endCell);
		endCell->inClosedSet = true;
		bool endCellUpdated = false;
		size_t oldSetSize = 0;
		while(oldSetSize < closedSet.size()) {
			oldSetSize = closedSet.size();
			auto & currentCell = closedSet.back();

			// DEBUG: avoid the cell itself, offmap cells, solid cells, and closedSet cells, respectively
			for (int row = -1; row <= 1; row++) {
				for (int col = -1; col <= 1; col++) {
					int nRow = currentCell->gridRow + row;
					int nCol = currentCell->gridCol + col;

					// check for invalid neighbors
					if ((row == 0 && col == 0) ||
						(nRow < 0 || nRow >= gridRows || nCol < 0 || nCol >= gridCols) ||
						gameGrid.cells[nRow][nCol].solid ||
						gameGrid.cells[nRow][nCol].inClosedSet) {
						continue;
					}

					// attempt to update the endCell
					GridCell_t * neighbor = &gameGrid.cells[nRow][nCol];
					if (neighbor->contents.empty() || (neighbor->contents.size() == 1 && neighbor->contents[0] == entity)) {
						endCell = neighbor;
						endCellUpdated = true;
						for (auto && cell : closedSet) {
							cell->inClosedSet = false;
						}
						break;
					} else {
						closedSet.push_back(neighbor);
						neighbor->inClosedSet = true;
					}
				}
				if (endCellUpdated)
					break;
			}
			if (endCellUpdated)
				break;
		}

		// empty the non-traversable sub-section from startCell to endCell
		// BUG(?): end variable is never path.end(), so it isn't checked for
		if (!endCellUpdated) {
			if (entity->path.size() > 1) {
				auto & end = std::find(entity->path.begin(), entity->path.end(), startCell);
				entity->path.erase(entity->path.begin(), end - 1);
				entity->currentWaypoint = &entity->path.back()->center;
			} else {
				entity->currentWaypoint = &entity->origin;
			}
			return false;
		} 
	}
/////////////////////////////////////////////////////////////////////////////////

	openSet.clear();
	closedSet.clear();

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

			// reset all the searched cells' costs
			// and set occupancy bools
			// to avoid affecting the next search
			for (auto && cell : openSet) {
				cell->gCost = 0;
				cell->hCost = 0;
				cell->fCost = 0;
				cell->inOpenSet = false;
			}
			for (auto && cell : closedSet) {
				cell->gCost = 0;
				cell->hCost = 0;
				cell->fCost = 0;
				cell->inClosedSet = false;
			}

			// TODO(elsewhere): while traversing the path checking for dynamic obstructions
			// ALSO check neighbors for better (newly re-calculated, then cleared) g,h,f costs
			// AND if one does have a better cost, then do the same pathfind update as if there were an obstruction
			// EXCEPT stop when/if the new path reaches a cell already on the old path (IE re-smooth the bumps created by dynamic obstacles)

			// TODO(elsewhere): chache an ULTIMATE/ORDERED endpoint in the entity
			// then while the entity walks, and the endpoint gets obstructed (hence changing the path.front() endpoint)
			// when the entity does its per-frame path traversal-check it can compare the path.front() cell to the ULTIMATE/ORDERED
			// endpoint and try to build a closer path if its freed up
			
			// build the path back (reverse iterator)
			size_t pathIndex = 0;
			while (currentCell != startCell) {			// FIXME(?): doesn't add a waypoint in the start cell

				// overwrite all old path points between the goal and new start
				if (entity->path.size() > pathIndex) {
					entity->path[pathIndex++] = currentCell;
				} else {
					entity->path.push_back(currentCell);
				}
				currentCell = currentCell->parent;
			}

			// erase any now-invalid waypoints beyond the new path's range
			// if not just updating a subsection
			if (startCell == entity->path.back() && entity->path.size() > pathIndex)
				entity->path.erase(entity->path.begin() + pathIndex, entity->path.end() );

			// set the the local goal
			entity->currentWaypoint = &entity->path.back()->center;
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
					!gameGrid.cells[nRow][nCol].contents.empty() ||
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
	// erase the non-traversable sub-section from startCell to endCell
	if (entity->path.size() > 1) {
		auto & end = std::find(entity->path.begin(), entity->path.end(), startCell);
		entity->path.erase(entity->path.begin(), end - 1);
		entity->currentWaypoint = &entity->path.back()->center;
	}
	else {
		entity->currentWaypoint = &entity->origin;
	}
	return false;
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
void UpdateBob(std::shared_ptr<GameObject_t> & entity) {
	if (entity->health > 0) {
		bool continueBob = (entity->velX || entity->velY);
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
bool CheckWaypointRange(std::shared_ptr<GameObject_t> & entity, SDL_Point & waypoint) {
	int xRange = SDL_abs((int)(entity->centerX - waypoint.x));
	int yRange = SDL_abs((int)(entity->centerY - waypoint.y));
	return (xRange <= entity->speed && yRange <= entity->speed);
}

//***************
// Normalize
// used for dynamic pathfinding
//***************
void Normalize(float & x, float & y) {
	float length = SDL_sqrtf(x * x + y * y);
	if (length) {
		float invLength = 1.0f / length;
		x *= invLength;
		y *= invLength;
	}
}

//***************
// UpdateOrigin
//***************
void UpdateOrigin(std::shared_ptr<GameObject_t>  & entity) {
	int dx = (int)nearbyintf(entity->speed * entity->velX);
	int dy = (int)nearbyintf(entity->speed * entity->velY);

	entity->origin.x += dx;
	entity->origin.y += dy;
	entity->bounds.x += dx;
	entity->bounds.y += dy;
	entity->centerX += nearbyintf(entity->speed * entity->velX);
	entity->centerY += nearbyintf(entity->speed * entity->velY);
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
// queries area dynamic contents prior to entering it
//***************
void GetAreaContents(const int centerRow, const int centerCol, std::vector<SDL_Rect *> & result, std::shared_ptr<GameObject_t> & ignore) {
	for (int row = -1; row <= 1; row++) {
		for(int col = -1; col <= 1; col++) {
			int checkRow = centerRow + row;
			int checkCol = centerCol + col;
			if (checkRow >= 0 && checkRow < gridRows && checkCol >= 0 && checkCol < gridCols) {
				auto & target = gameGrid.cells[checkRow][checkCol];
				if (!target.solid) {
					for (auto && entity : target.contents) {
						if (entity == ignore)
							continue;
						result.push_back(&entity->bounds);
					}
				}
			}
		}
	}
}

//***************
// TranslateRect
// dynamic pathfinding utility
//***************
void TranslateRect(SDL_Rect & target, float dx, float dy) {
	target = {	(int)nearbyintf(target.x + dx),
				(int)nearbyintf(target.y + dy),
				target.w,
				target.h };
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
// Redirect
// dynamic pathfinding
//***************
void Redirect(std::shared_ptr<GameObject_t> & entity, std::vector<SDL_Rect *> & areaContents) {
	// perform a modified A* that generates N (8-360) neighboring rectangles then does g,h,f cost with the
	// next waypoint as the goal, and the heuristic does the same 14/10 evaluation based on
	// the rect's x/y center difference from the goal divided by entity->speed
	// note: get the rects by rotating and translating the entity->bounds according to speed
	// and add **copies** to a vector (instead of references because these rects won't exist on the next iteration)
}

//***************
// Move
// dynamic pathfinding
//***************
void Move(std::shared_ptr<GameObject_t> & entity) {
	Uint32 dt = SDL_GetTicks() - entity->moveTime;

	if (dt >= 25 && !entity->path.empty()) {
		entity->moveTime = SDL_GetTicks();

		if (entity->currentWaypoint == &entity->origin)
			entity->currentWaypoint = &entity->path.back()->center;

		// set velocity based on next point in path
		if (!CheckWaypointRange(entity, *entity->currentWaypoint)) {

// BEGIN FREEHILL path cell traversal and update test
			GridCell_t * prevGood = entity->path.back();
			bool validPath = true;
			for (auto && cell = entity->path.rbegin(); cell != entity->path.rend(); cell++) {
				// check if any cell along the path has become obstructed, 
				// and if so then pathfind from the last unobstructed cell to the goal cell
				// DEBUG: ignore the entity itself as part of the cell contents check
				if ((*cell)->contents.empty() || ((*cell)->contents.size() == 1 && (*cell)->contents[0] == entity)) {
					prevGood = *cell;
				} else if (prevGood != entity->path.back()) {
					validPath = PathFind(entity, prevGood->center, entity->path.front()->center);		
					break;
				}
			}
// END FREEHILL path cell traversal and update test

			// determine the intended movement direction
			if (validPath) {
				float waypointVX = (float)(entity->currentWaypoint->x - entity->centerX);
				float waypointVY = (float)(entity->currentWaypoint->y - entity->centerY);
				Normalize(waypointVX, waypointVY);
				entity->velX = waypointVX;
				entity->velY = waypointVY;
			} else {
				entity->velX = 0.0f;
				entity->velY = 0.0f;
			}
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
		} else {		
			// close enough to a waypoint or no waypoints available
			entity->path.pop_back();
			entity->velX = 0.0f;
			entity->velY = 0.0f;
			entity->currentWaypoint = &entity->origin;
		}
		UpdateOrigin(entity);
		// if the entity moved, then update gameGrid and internal cell lists for collision filtering
		if (entity->velX || entity->velY)
			UpdateCellReferences(entity);

		UpdateBob(entity);		// DEBUG: bob does not affect cell location
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
	// TODO: Goodmans strategy/state here
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
						PathFind(entity, SDL_Point{ (int)entity->centerX, (int)entity->centerY }, second);
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