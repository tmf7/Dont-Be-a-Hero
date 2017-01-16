//**********************************************
//*
//* Copyright Thomas Freehill January 14 2017
//* Don't Be a Hero (game)
//* 
//**********************************************

#include <Windows.h>
#include "Definitions.h"

// rendering info
SDL_Window * window;
SDL_Renderer * renderer;
TTF_Font * font;
const SDL_Color clearColor = { 128, 128, 128, 255 };
const SDL_Color transparentGray = { 0, 0, 0, 64 };
const SDL_Color opaqueGreen = { 0, 255, 0, 255 };

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

// GameObject_t
typedef struct GameObject_s {
	SDL_Point	origin;			// top-left of sprite image
	SDL_Rect	bounds;			// world-position and size of collision box
	int			speed;
	SDL_Point	velocity;

	int			bob;			// the illusion of walking
	bool		bobMaxed;		// peak bob height, return to 0
	Uint32		bobTime;		// delay between bob updates

	int			facing;			// left or right to determine flip
	int			health;			// health <= 0 rotates sprite 90 and color-blends gray, hit color-blends red
	int			stamina;		// subtraction color-blends blue for a few frames, stamina <= 0 blinks blue until full
	bool		damaged;
	bool		fatigued;
	Uint32		blinkTime;		// future point to stop color mod

	std::string name;			// use: SDL_RenderCopy(	renderer,
								//						spriteSheet.texture, 
								//						spriteSheet.frames[tileSet.frameAtlas[spriteName]],
								//						&dstRectSameSizeAsSrcFrameButDifferentXY);
	GameObject_s() 
		:	origin({0, 0}),
			bounds({0, 0, 0, 0}),
			speed(0),
			velocity({0, 0}),
			name("invalid"),
			bob(0),
			bobMaxed(false),
			bobTime(0),
			facing(false),
			blinkTime(0),
			health(0),
			stamina(0),
			damaged(false),
			fatigued(false) {
	};

	GameObject_s(const SDL_Point & origin,  const std::string & name) 
		:	origin(origin),
			velocity({ 0, 0 }),
			name(name),
			bob(0),
			bobMaxed(false),
			bobTime(0),
			facing(rand() % 2),
			blinkTime(0),
			damaged(false),
			fatigued(false) {
		bounds = {origin.x + 4, origin.y + 4, 8, 16 };
		if (name == "goodman") {
			health = 100;
			stamina = 100;
			speed = 4;
		} else if (name.find(std::string("melee")) || name.find(std::string("ranged"))) {
			health = 2;
			stamina = -1;
			speed = 2;
		} else { // missile
			health = 1;
			stamina = -1;
			speed = 3;
		}
	};
} GameObject_t;

// entities
std::unordered_map<std::string, int> entityAtlas;		// entity lookup by name
std::vector<std::shared_ptr<GameObject_t>> entities;	// all dynamically allocated game objects
int entityGUID = 0;

// GridCell_t
typedef struct GridCell_s {
	bool solid;												// triggers collision
	SDL_Rect bounds;										// world location and cell size
	std::vector<std::shared_ptr<GameObject_t>> contents;	// items, monsters, terrain/obstacles, or Goodman
} GridCell_t;

// grid dimensions
const SDL_Point	defaultGridOrigin = { 0, 0 };
const int gameWidth		= 800;
const int gameHeight	= 600;
const int cellSize		= 16;	// 16x16 square cells
const int gridRows		= gameWidth / cellSize;
const int gridCols		= (gameHeight / cellSize) + 1;

// gameGrid
struct {
	SDL_Texture * texture;
	std::array<std::array<GridCell_t, gridCols>, gridRows> cells;	// spatial partitioning of play area
} gameGrid;

// selection
std::vector<GridCell_t *> selection;	// all interior and border cells of selected area

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
		DrawRect(entity->bounds, opaqueGreen, false);		
	}
}


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
			SDL_Rect & targetCell = gameGrid.cells[row][col].bounds;
			targetCell = { row * cellSize,  col * cellSize, cellSize, cellSize };
			DrawRect(targetCell, transparentGray, false);
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
	std::shared_ptr<GameObject_t> goodman = std::make_shared<GameObject_t>(SDL_Point{ spawnX, spawnY }, name);
	entities.push_back(goodman);
	entityAtlas[name] = entityGUID++;
	gameGrid.cells[spawnRow][spawnCol].contents.push_back(goodman);
	// TODO: traverse all entities each frame and update the cells each belongs to (more than one)
	// ALSO update the gameGrid.cells.contents for fast collision checks
}

//***************
// SpawnMonsters
// TODO: update this for multiple monsters
//***************
void SpawnMonsters() {
	int spawnX = 200;
	int spawnY = 200;
	int spawnRow = spawnX / cellSize;
	int spawnCol = spawnY / cellSize;
	// TODO: make this a random location in the grid (that isn't a collision tile) (and is farthest from goodman)
	std::string name = "melee_";
	name += std::to_string(entityGUID);
	std::shared_ptr<GameObject_t> melee = std::make_shared<GameObject_t>(SDL_Point{ spawnX, spawnY }, name);
	entities.push_back(melee);
	entityAtlas[name] = entityGUID++;
	gameGrid.cells[spawnRow][spawnCol].contents.push_back(melee);
	// TODO: traverse all entities each frame and update the cells each belongs to (more than one)
	// ALSO update the gameGrid.cells.contents for fast collision checks
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

//***************
// UpdateBob
//***************
void UpdateBob(std::shared_ptr<GameObject_t> entity) {
	Uint32 dt = SDL_GetTicks() - entity->bobTime;
	if (entity->health > 0 && dt > 50) {
		bool continueBob = (entity->velocity.x > 0 || entity->velocity.y > 0);
		if (continueBob && !entity->bobMaxed) {
			entity->bobMaxed = (++entity->bob >= 5) ? true : false;
			entity->origin.y--;
			entity->bounds.y--;
		} else if (entity->bob > 0) {
			entity->bobMaxed = (--entity->bob > 0) ? true : false;
			entity->origin.y++;
			entity->bounds.y++;
		}
		entity->bobTime = SDL_GetTicks();
	}
}

//***************
// SelectArea
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

	// build the list of selected cells
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
			case SDL_QUIT:
				running = false;
				break;
			case SDL_MOUSEMOTION:
				mouseX = event.motion.x;
				mouseY = event.motion.y;
				second = { mouseX, mouseY };
				break;
			case SDL_MOUSEBUTTONDOWN:
				first = { event.button.x, event.button.y };
				beginSelection = true;
				ClearAreaSelection();
				break;
			case SDL_MOUSEBUTTONUP:
				if (beginSelection) {
					beginSelection = false;
					SelectArea(first, second);
				}
				entities[1]->blinkTime = SDL_GetTicks() + 1000;
				entities[1]->damaged = true;
				entities[1]->health = -10;
				break;
			default:
				break;
			}
		}

		if (!beginSelection)
			first = second;

		// begin drawing
		SDL_RenderClear(renderer);

		// draw the map
		SDL_RenderCopy(renderer, map.texture, NULL, &map.frame);

		// draw the grid texture
//		DrawGameGrid();

		// draw the collision layer
		DrawCollision();

		// test entity bob
		auto & test = entities[entityAtlas["goodman"]];
		test->velocity.x = 1;
		UpdateBob(test);

		auto & test2 = entities[entityAtlas["melee_1"]];
		test2->velocity.x = 1;
		UpdateBob(test2);

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