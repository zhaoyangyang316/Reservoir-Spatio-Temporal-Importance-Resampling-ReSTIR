#include "InitLightPlusTemporalPass.h"

// Some global vars, used to simplify changing shader location & entry points
namespace {
	// Where is our shader located?
	const char* kFileRayTrace = "Tutorial11\\initLightPlusTemporal.rt.hlsl";

	// What are the entry points in that shader for various ray tracing shaders?
	const char* kEntryPointRayGen  = "LambertShadowsRayGen";
	const char* kEntryPointMiss0   = "ShadowMiss";
	const char* kEntryAoAnyHit     = "ShadowAnyHit";
	const char* kEntryAoClosestHit = "ShadowClosestHit";
};

bool InitLightPlusTemporalPass::initialize(RenderContext* pRenderContext, ResourceManager::SharedPtr pResManager)
{
	// Stash a copy of our resource manager so we can get rendering resources
	mpResManager = pResManager;
	// TODO: DELETE Reservoir2
	mpResManager->requestTextureResources({ "WorldPosition", "WorldNormal", "MaterialDiffuse", "Reservoir", "Reservoir2"});
	mpResManager->requestTextureResource(ResourceManager::kOutputChannel);

	// Set the default scene to load
	mpResManager->setDefaultSceneName("Data/pink_room/pink_room.fscene");

	// Create our wrapper around a ray tracing pass.  Tell it where our ray generation shader and ray-specific shaders are
	mpRays = RayLaunch::create(kFileRayTrace, kEntryPointRayGen);
	mpRays->addMissShader(kFileRayTrace, kEntryPointMiss0);
	mpRays->addHitShader(kFileRayTrace, kEntryAoClosestHit, kEntryAoAnyHit);
	mpRays->compileRayProgram();
	if (mpScene) mpRays->setScene(mpScene);
    return true;
}

bool InitLightPlusTemporalPass::hasCameraMoved()
{
	// Has our camera moved?
	return mpScene &&                      // No scene?  Then the answer is no
		mpScene->getActiveCamera() &&   // No camera in our scene?  Then the answer is no
		(mpLastCameraMatrix != mpScene->getActiveCamera()->getViewMatrix());   // Compare the current matrix with the last one
}

void InitLightPlusTemporalPass::initScene(RenderContext* pRenderContext, Scene::SharedPtr pScene)
{
	// Stash a copy of the scene and pass it to our ray tracer (if initialized)
    mpScene = std::dynamic_pointer_cast<RtScene>(pScene);

	// Grab a copy of the current scene's camera matrix (if it exists)
	if (mpScene && mpScene->getActiveCamera())
		mpLastCameraMatrix = mpScene->getActiveCamera()->getViewMatrix();

	if (mpRays) mpRays->setScene(mpScene);
}

void InitLightPlusTemporalPass::execute(RenderContext* pRenderContext)
{
	// Get the output buffer we're writing into; clear it to black.
	Texture::SharedPtr pDstTex = mpResManager->getClearedTexture(ResourceManager::kOutputChannel, vec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Do we have all the resources we need to render?  If not, return
	if (!pDstTex || !mpRays || !mpRays->readyToRender()) return;

	// If the camera in our current scene has moved, we want to reset mInitLightPerPixel
	if (hasCameraMoved())
	{
		mInitLightPerPixel = true;
		mpLastCameraMatrix = mpScene->getActiveCamera()->getViewMatrix();
	}

	// Set our ray tracing shader variables 
	auto rayGenVars = mpRays->getRayGenVars();
	rayGenVars["RayGenCB"]["gMinT"]       = mpResManager->getMinTDist();
	rayGenVars["RayGenCB"]["gFrameCount"] = mFrameCount++;
	// For ReSTIR - update the toggle in the shader
	rayGenVars["RayGenCB"]["gInitLight"]  = mInitLightPerPixel; 
	rayGenVars["RayGenCB"]["gTemporalReuse"] = mTemporalReuse;

	// Pass our G-buffer textures down to the HLSL so we can shade
	rayGenVars["gPos"]         = mpResManager->getTexture("WorldPosition");
	rayGenVars["gNorm"]        = mpResManager->getTexture("WorldNormal");
	rayGenVars["gDiffuseMatl"] = mpResManager->getTexture("MaterialDiffuse");
	// For ReSTIR - update the buffer storing reservoir (weight sum, chosen light index, number of candidates seen) 
	rayGenVars["gReservoir"]   = mpResManager->getTexture("Reservoir"); 
	// TODO: DELETE Reservoir2
	rayGenVars["gReservoir2"] = mpResManager->getTexture("Reservoir2");
	rayGenVars["gOutput"]      = pDstTex;

	// Shoot our rays and shade our primary hit points
	mpRays->execute( pRenderContext, mpResManager->getScreenSize() );

	// For ReSTIR - toggle to false so we only sample a random candidate for the first frame
	mInitLightPerPixel = false;
}


