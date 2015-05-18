// 
// Copyright 2015 Jeff Bush
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// 


#include <stdio.h>
#include <SIMDMath.h>
#include <RenderContext.h>
#include <Surface.h>
#include <schedule.h>
#include "PakFile.h"
#include "TextureShader.h"

using namespace librender;

RenderBspNode *findNode(RenderBspNode *head, float x, float y, float z)
{
	RenderBspNode *node = head;
	do
	{
		if (node->pointInFront(x, y, z))
			node = node->frontChild;
		else
			node = node->backChild;
	}
	while (node->frontChild);

	return node;
}

void markAllAncestors(RenderBspNode *node, int index)
{
	while (node)
	{
		node->markNumber = index;
		node = node->parent;
	}
}

void markLeaves(PakFile &pak, const uint8_t *pvsList, int index, int numLeaves, int markNumber)
{
	const uint8_t *pvs = pvsList + index;
	int currentLeaf = 0;
	while (currentLeaf < numLeaves)
	{
		if (*pvs == 0)
		{
			// Skip
			currentLeaf += pvs[1] * 8;
			pvs += 2;
			continue;
		}
		
		for (int mask = 1; mask <= 0x80 && currentLeaf < numLeaves; mask <<= 1)
		{
			if (*pvs & mask)
				markAllAncestors(pak.getLeafBspNode(currentLeaf), markNumber);
			
			currentLeaf++;
		}
		
		pvs++;
	}
}


// Render from front to back to take advantage of early-Z 
void renderRecursive(RenderContext *context, const RenderBspNode *node, const Vec3 &camera, int markNumber)
{
	if (!node->frontChild)
	{
		// Leaf node
		context->bindGeometry(&node->leaf->vertexBuffer, &node->leaf->indexBuffer);
		context->submitDrawCommand();
	}
	else if (node->pointInFront(camera[0], camera[1], camera[2]))
	{
		if (node->frontChild->markNumber == markNumber)
			renderRecursive(context, node->frontChild, camera, markNumber);

		if (node->backChild->markNumber == markNumber)
			renderRecursive(context, node->backChild, camera, markNumber);
	}
	else
	{
		if (node->backChild->markNumber == markNumber)
			renderRecursive(context, node->backChild, camera, markNumber);

		if (node->frontChild->markNumber == markNumber)
			renderRecursive(context, node->frontChild, camera, markNumber);
	}
}


// All threads start execution here.
int main()
{
	if (__builtin_nyuzi_read_control_reg(0) != 0)
		workerThread();
	
	// Set up render state
	RenderContext *context = new RenderContext(0x1000000);
	RenderTarget *renderTarget = new RenderTarget();
	Surface *colorBuffer = new Surface(FB_WIDTH, FB_HEIGHT, (void*) 0x200000);
	Surface *zBuffer = new Surface(FB_WIDTH, FB_HEIGHT);
	renderTarget->setColorBuffer(colorBuffer);
	renderTarget->setDepthBuffer(zBuffer);
	context->bindTarget(renderTarget);
	context->enableDepthBuffer(true);
	context->bindShader(new TextureShader());
	
	PakFile pak;
	
	pak.open("pak0.pak");
	pak.readBsp("maps/e1m1.bsp");
	RenderBspNode *root = pak.getBspTree();

	context->bindTexture(0, pak.getTexture());
	context->enableWireframeMode(false);

	// Start worker threads
	__builtin_nyuzi_write_control_reg(30, 0xffffffff);
	
	TextureUniforms uniforms;
	Matrix projectionMatrix = Matrix::getProjectionMatrix(FB_WIDTH, FB_HEIGHT);

	Vec3 cameraPos(544, 288, 32);
	for (int frame = 0; ; frame++)
	{
		Matrix modelViewMatrix = Matrix::lookAt(cameraPos, cameraPos + Vec3(cos(frame * 3.14 / 8), 
			sin(frame * 3.14 / 8), 0), Vec3(0, 0, 1));
		uniforms.fMVPMatrix = projectionMatrix * modelViewMatrix;
		context->bindUniforms(&uniforms, sizeof(uniforms));

		RenderBspNode *currentNode = findNode(root, cameraPos[0], cameraPos[1], cameraPos[2]);
		markLeaves(pak, pak.getPvsList(), currentNode->pvsIndex, pak.getNumLeaves(), frame);
		renderRecursive(context, root, cameraPos, frame);

		int startInstructions = __builtin_nyuzi_read_control_reg(6);
		context->finish();
		printf("rendered frame in %d instructions.\n", __builtin_nyuzi_read_control_reg(6) 
			- startInstructions);
	}
}
