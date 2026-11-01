#ifndef NET_MINECRAFT_CLIENT_RENDERER__RenderList_H__
#define NET_MINECRAFT_CLIENT_RENDERER__RenderList_H__

//package net.minecraft.client.renderer;

class RenderChunk;

class RenderList
{
	static const int MAX_NUM_OBJECTS = 1024 * 8;

public:
	RenderList();
	~RenderList();

    void init(float xOff, float yOff, float zOff);

	void add(int list);
	void addR(const RenderChunk& chunk);

	__inline void next() { if (listIndex < MAX_NUM_OBJECTS) ++listIndex; }

    void render();
	void renderChunks();

    void clear();


	float xOff, yOff, zOff;
	int* lists;
	RenderChunk* rlists;

	int listIndex;
	bool inited;
	bool rendered;

private:
	int bufferLimit;
};

#endif /*NET_MINECRAFT_CLIENT_RENDERER__RenderList_H__*/
