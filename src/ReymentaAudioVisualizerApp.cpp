/*
                
        Basic Spout sender for Cinder

        Search for "SPOUT" to see what is required
        Uses the Spout dll

        Based on the RotatingBox CINDER example without much modification
        Nothing fancy about this, just the basics.

        Search for "SPOUT" to see what is required

    ==========================================================================
    Copyright (C) 2014 Lynn Jarvis.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    ==========================================================================

*/
/*
Copyright (c) 2014, Paul Houx - All rights reserved.
This code is intended for use with the Cinder C++ library: http://libcinder.org

Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and
the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/gl/Texture.h"
#include "cinder/gl/Vbo.h"
#include "cinder/Camera.h"
#include "cinder/Channel.h"
#include "cinder/ImageIo.h"
#include "cinder/MayaCamUI.h"
#include "cinder/Rand.h"
#include "cinder/ImageIo.h"
// audio
#include "cinder/audio/Context.h"
#include "cinder/audio/MonitorNode.h"
#include "cinder/audio/Utilities.h"
#include "cinder/audio/Source.h"
#include "cinder/audio/Target.h"
#include "cinder/audio/dsp/Converter.h"
#include "cinder/audio/SamplePlayerNode.h"
#include "cinder/audio/SampleRecorderNode.h"
#include "cinder/audio/NodeEffects.h"
#include "cinder/audio/MonitorNode.h"
//#include "AudioDrawUtils.h"
#include "cinder/gl/Vbo.h"
#include <vector>

// spout
#include "spout.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class ReymentaAudioVisualizerApp : public AppNative {
public:
    void prepareSettings(Settings *settings);
	void setup();
	void update();
	void draw();
    void shutdown();
	void mouseDown(MouseEvent event);
	void mouseDrag(MouseEvent event);
	void mouseUp(MouseEvent event);
	void fileDrop(FileDropEvent event);
	void loadWaveFile(string aFilePath);

	bool				signalChannelEnd;

private:
    // -------- SPOUT -------------
    SpoutSender spoutsender;                    // Create a Spout sender object
    bool bInitialized;                          // true if a sender initializes OK
    bool bMemoryMode;                           // tells us if texture share compatible
    unsigned int g_Width, g_Height;             // size of the texture being sent out
    char SenderName[256];                       // sender name 
    gl::Texture spoutTexture;                   // Local Cinder texture used for sharing
    // ----------------------------
	// width and height of our mesh
	static const int kWidth = 512;
	static const int kHeight = 512;

	// number of frequency bands of our spectrum
	static const int kBands = 1024;
	static const int kHistory = 128;

	Channel32f			mChannelLeft;
	Channel32f			mChannelRight;
	CameraPersp			mCamera;
	MayaCamUI			mMayaCam;
	Vec2f						mCamPosXY;

	gl::GlslProg		mShader;
	gl::Texture			mTextureLeft;
	gl::Texture			mTextureRight;
	gl::Texture::Format	mTextureFormat;
	gl::VboMesh			mMesh;
	uint32_t			mOffset;

	bool				mIsMouseDown;
	bool				mIsAudioPlaying;
	double				mMouseUpTime;
	double				mMouseUpDelay;

	vector<string>		mAudioExtensions;
	fs::path			mAudioPath;
	// audio
	audio::InputDeviceNodeRef		mLineIn;
	audio::MonitorSpectralNodeRef	mMonitorLineInSpectralNode;
	audio::MonitorSpectralNodeRef	mMonitorWaveSpectralNode;

	vector<float>					mMagSpectrum;
	// audio

	audio::SamplePlayerNodeRef		mSamplePlayerNode;
	audio::SourceFileRef			mSourceFile;
	float						*mData;
	float						maxVolume;
	bool						mUseLineIn;
	float						mAudioMultFactor;
	float						iFreqs[4];

};

// -------- SPOUT -------------
void ReymentaAudioVisualizerApp::prepareSettings(Settings *settings)
{
        g_Width  = 640;
        g_Height = 512;
        settings->setWindowSize( g_Width, g_Height );
        settings->setFullScreen( false );
        settings->setResizable( false ); // keep the screen size constant for a sender
        settings->setFrameRate( 60.0f );
}
// ----------------------------

void ReymentaAudioVisualizerApp::setup()
{
	maxVolume = 0.0f;
	mUseLineIn = true;
	mAudioMultFactor = 100.0f;
	mData = new float[1024];
	for (int i = 0; i < 1024; i++)
	{
		mData[i] = 0;
	}
	for (int i = 0; i < 4; i++)
	{
		iFreqs[i] = i;
	}
	mCamPosXY = Vec2f::zero();

	// linein
	auto ctx = audio::Context::master();
	mLineIn = ctx->createInputDeviceNode();

	auto scopeLineInFmt = audio::MonitorSpectralNode::Format().fftSize(2048).windowSize(1024);
	mMonitorLineInSpectralNode = ctx->makeNode(new audio::MonitorSpectralNode(scopeLineInFmt));

	mLineIn >> mMonitorLineInSpectralNode;

	mLineIn->enable();

	// wave
	// TODO: it is pretty surprising when you recreate mScope here without checking if there has already been one added.
	//	- user will no longer see the old mScope, but the context still owns a reference to it, so another gets added each time we call this method.
	auto scopeWaveFmt = audio::MonitorSpectralNode::Format().fftSize(2048).windowSize(1024);
	mMonitorWaveSpectralNode = ctx->makeNode(new audio::MonitorSpectralNode(scopeWaveFmt));

	ctx->enable();
	// initialize signals
	signalChannelEnd = false;

	// make a list of valid audio file extensions and initialize audio variables
	const char* extensions[] = { "mp3", "wav", "ogg" };
	mAudioExtensions = vector<string>(extensions, extensions + 2);
	mAudioPath = getAssetPath("");
	mIsAudioPlaying = false;

	// setup camera
	mCamera.setPerspective(50.0f, 1.0f, 1.0f, 10000.0f);
	mCamera.setEyePoint(Vec3f(-kWidth / 4, kHeight / 2, -kWidth / 8));
	mCamera.setCenterOfInterestPoint(Vec3f(kWidth / 4, -kHeight / 8, kWidth / 4));

	// create channels from which we can construct our textures
	mChannelLeft = Channel32f(kBands, kHistory);
	mChannelRight = Channel32f(kBands, kHistory);
	memset(mChannelLeft.getData(), 0, mChannelLeft.getRowBytes() * kHistory);
	memset(mChannelRight.getData(), 0, mChannelRight.getRowBytes() * kHistory);

	// create texture format (wrap the y-axis, clamp the x-axis)
	mTextureFormat.setWrapS(GL_CLAMP);
	mTextureFormat.setWrapT(GL_REPEAT);
	mTextureFormat.setMinFilter(GL_LINEAR);
	mTextureFormat.setMagFilter(GL_LINEAR);

	// compile shader
	try {
		mShader = gl::GlslProg(loadAsset("shaders/spectrum.vert"), loadAsset("shaders/spectrum.frag"));
	}
	catch (const std::exception& e) {
		console() << e.what() << std::endl;
		quit();
		return;
	}

	// create static mesh (all animation is done in the vertex shader)
	std::vector<Vec3f>	vertices;
	std::vector<Colorf>	colors;
	std::vector<Vec2f>	coords;
	std::vector<size_t>	indices;

	for (size_t h = 0; h<kHeight; ++h)
	{
		for (size_t w = 0; w<kWidth; ++w)
		{
			// add polygon indices
			if (h < kHeight - 1 && w < kWidth - 1)
			{
				size_t offset = vertices.size();

				indices.push_back(offset);
				indices.push_back(offset + kWidth);
				indices.push_back(offset + kWidth + 1);
				indices.push_back(offset);
				indices.push_back(offset + kWidth + 1);
				indices.push_back(offset + 1);
			}

			// add vertex
			vertices.push_back(Vec3f(float(w), 0, float(h)));

			// add texture coordinates
			// note: we only want to draw the lower part of the frequency bands,
			//  so we scale the coordinates a bit
			const float part = 0.5f;
			float s = w / float(kWidth - 1);
			float t = h / float(kHeight - 1);
			coords.push_back(Vec2f(part - part * s, t));

			// add vertex colors
			colors.push_back(Color(CM_HSV, s, 0.5f, 0.75f));
		}
	}

	gl::VboMesh::Layout layout;
	layout.setStaticPositions();
	layout.setStaticColorsRGB();
	layout.setStaticIndices();
	layout.setStaticTexCoords2d();

	mMesh = gl::VboMesh(vertices.size(), indices.size(), layout, GL_TRIANGLES);
	mMesh.bufferPositions(vertices);
	mMesh.bufferColorsRGB(colors);
	mMesh.bufferIndices(indices);
	mMesh.bufferTexCoords2d(0, coords);

	mIsMouseDown = false;
	mMouseUpDelay = 30.0;
	mMouseUpTime = getElapsedSeconds() - mMouseUpDelay;

	// the texture offset has two purposes:
	//  1) it tells us where to upload the next spectrum data
	//  2) we use it to offset the texture coordinates in the shader for the scrolling effect
	mOffset = 0;    glEnable(GL_TEXTURE_2D);
    gl::enableDepthRead();
    gl::enableDepthWrite(); 

    // -------- SPOUT -------------
    // Set up the texture we will use to send out
    // We grab the screen so it has to be the same size
    spoutTexture =  gl::Texture(g_Width, g_Height);
    strcpy_s(SenderName, "Reymenta Audio visualizer Sender"); // we have to set a sender name first
    // Optionally test for texture share compatibility
    // bMemoryMode informs us whether Spout initialized for texture share or memory share
    bMemoryMode = spoutsender.GetMemoryShareMode();
    // Initialize a sender
    bInitialized = spoutsender.CreateSender(SenderName, g_Width, g_Height);
    // ----------------------------
}

void ReymentaAudioVisualizerApp::update()
{
	if (mUseLineIn || !mSamplePlayerNode)
	{
		mMagSpectrum = mMonitorLineInSpectralNode->getMagSpectrum();
	}
	else
	{
		mMagSpectrum = mMonitorWaveSpectralNode->getMagSpectrum();
	}
	if (mMagSpectrum.empty())
		return;
	unsigned char signal[kBands];
	maxVolume = 0.0;
	size_t mDataSize = mMagSpectrum.size();
	if (mDataSize > 0)
	{
		float mv;
		float db;
		float maxdb = 0.0f;
		for (size_t i = 0; i < mDataSize; i++) {
			float f = mMagSpectrum[i];
			db = audio::linearToDecibel(f);
			f = db * mAudioMultFactor;
			if (f > maxVolume)
			{
				maxVolume = f; mv = f;
			}
			mData[i] = f;
			int ger = f;
			signal[i] = static_cast<unsigned char>(ger);

			if (db > maxdb) maxdb = db;

			switch (i)
			{
			case 11:
				iFreqs[0] = f;
				break;
			case 13:
				iFreqs[1] = f;
				break;
			case 15:
				iFreqs[2] = f;
				break;
			case 18:
				iFreqs[3] = f;
				break;
			default:
				break;
			}

		}
		// store it as a 512x2 texture in UPDATE only!!
		//mTextures->setAudioTexture(signal);
	}

	// reset FMOD signals
	//signalChannelEnd = false;

	// get spectrum for left and right channels and copy it into our channels
	float* pDataLeft = mChannelLeft.getData() + kBands * mOffset;
	float* pDataRight = mChannelRight.getData() + kBands * mOffset;

	for (size_t i = 0; i < mDataSize; i++) {
		pDataLeft[i] = mMagSpectrum[i] * mAudioMultFactor;
		pDataRight[i] = mMagSpectrum[i] * mAudioMultFactor;
	}

	// increment texture offset
	mOffset = (mOffset + 1) % kHistory;

	// clear the spectrum for this row to avoid old data from showing up
	pDataLeft = mChannelLeft.getData() + kBands * mOffset;
	pDataRight = mChannelRight.getData() + kBands * mOffset;
	memset(pDataLeft, 0, kBands * sizeof(float));
	memset(pDataRight, 0, kBands * sizeof(float));

	// animate camera if mouse has not been down for more than 30 seconds
	if (!mIsMouseDown && (getElapsedSeconds() - mMouseUpTime) > mMouseUpDelay)
	{
		float t = float(getElapsedSeconds());
		float x = 0.5f + 0.5f * math<float>::cos(t * 0.07f);
		float y = 0.1f - 0.2f * math<float>::sin(t * 0.09f);
		float z = 0.25f * math<float>::sin(t * 0.05f) - 0.25f;
		Vec3f eye = Vec3f(kWidth * x, kHeight * y, kHeight * z);

		x = 1.0f - x;
		y = -0.3f;
		z = 0.6f + 0.2f *  math<float>::sin(t * 0.12f);
		Vec3f interest = Vec3f(kWidth * x, kHeight * y, kHeight * z);

		// gradually move to eye position and center of interest
		mCamera.setEyePoint(eye.lerp(0.995f, mCamera.getEyePoint()));
		mCamera.setCenterOfInterestPoint(interest.lerp(0.990f, mCamera.getCenterOfInterestPoint()));
	}
}

void ReymentaAudioVisualizerApp::draw()
{
	gl::clear();

	// use camera
	gl::pushMatrices();
	gl::setMatrices(mCamera);
	{
		// bind shader
		mShader.bind();
		mShader.uniform("uTexOffset", mOffset / float(kHistory));
		mShader.uniform("uLeftTex", 0);
		mShader.uniform("uRightTex", 1);

		// create textures from our channels and bind them
		mTextureLeft = gl::Texture(mChannelLeft, mTextureFormat);
		mTextureRight = gl::Texture(mChannelRight, mTextureFormat);

		mTextureLeft.enableAndBind();
		mTextureRight.bind(1);

		// draw mesh using additive blending
		gl::enableAdditiveBlending();
		gl::color(Color(1, 1, 1));
		gl::draw(mMesh);
		gl::disableAlphaBlending();

		// unbind textures and shader
		mTextureRight.unbind();
		mTextureLeft.unbind();
		mShader.unbind();
	}
	gl::popMatrices();

    // -------- SPOUT -------------
    if(bInitialized) {

        // Grab the screen (current read buffer) into the local spout texture
        spoutTexture.bind();
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_Width, g_Height);
        spoutTexture.unbind();

        // Send the texture for all receivers to use
        // NOTE : if SendTexture is called with a framebuffer object bound, that binding will be lost
        // and has to be restored afterwards because Spout uses an fbo for intermediate rendering
        spoutsender.SendTexture(spoutTexture.getId(), spoutTexture.getTarget(), g_Width, g_Height);

    }

    // Show the user what it is sending
    char txt[256];
    sprintf_s(txt, "Sending as [%s]", SenderName);
    gl::setMatricesWindow( getWindowSize() );
    gl::enableAlphaBlending();
    gl::drawString( txt, Vec2f( toPixels( 20 ), toPixels( 20 ) ), Color( 1, 1, 1 ), Font( "Verdana", toPixels( 24 ) ) );
    sprintf_s(txt, "fps : %2.2d", (int)getAverageFps());
    gl::drawString( txt, Vec2f(getWindowWidth() - toPixels( 100 ), toPixels( 20 ) ), Color( 1, 1, 1 ), Font( "Verdana", toPixels( 24 ) ) );
    gl::disableAlphaBlending();
    // ----------------------------
}
void ReymentaAudioVisualizerApp::fileDrop(FileDropEvent event)
{
	string ext = "";
	// use the last of the dropped files
	boost::filesystem::path mPath = event.getFile(event.getNumFiles() - 1);
	//string mFile = event.getFile(event.getNumFiles() - 1).string();
	string mFile = mPath.string();
	if (mFile.find_last_of(".") != std::string::npos) ext = mFile.substr(mFile.find_last_of(".") + 1);
	if (ext == "wav" || ext == "mp3")
	{
		loadWaveFile(mFile);
		getWindow()->setTitle("Reymenta - " + mFile);
	}
}
void ReymentaAudioVisualizerApp::loadWaveFile(string aFilePath)
{
	try
	{
		if (!fs::exists(aFilePath))
		{
			return;

		}
		else
		{
			auto ctx = audio::master();
			mSourceFile = audio::load(loadFile(aFilePath), audio::master()->getSampleRate());
			mSamplePlayerNode = ctx->makeNode(new audio::FilePlayerNode(mSourceFile, false));
			mSamplePlayerNode->setLoopEnabled(false);
			mSamplePlayerNode >> mMonitorWaveSpectralNode >> ctx->getOutput();
			mSamplePlayerNode->enable();
			//ctx->enable();

			// or connect in series (it is added to the Context's 'auto pulled list')
			//mSamplePlayerNode >> ctx->getOutput();
			mSamplePlayerNode->seek(0);

			auto filePlayer = dynamic_pointer_cast<audio::FilePlayerNode>(mSamplePlayerNode);
			CI_ASSERT_MSG(filePlayer, "expected sample player to be either BufferPlayerNode or FilePlayerNode");

			filePlayer->setSourceFile(mSourceFile);

			//audio::master()->printGraph();

			mSamplePlayerNode->start();
			mUseLineIn = false;
		}
	}
	catch (...)
	{

	}
}
void ReymentaAudioVisualizerApp::mouseDown(MouseEvent event)
{
	// handle mouse down
	mIsMouseDown = true;

	mMayaCam.setCurrentCam(mCamera);
	mMayaCam.mouseDown(mCamPosXY);
}

void ReymentaAudioVisualizerApp::mouseDrag(MouseEvent event)
{
	// handle mouse drag
	mMayaCam.mouseDrag(mCamPosXY, event.isLeftDown(), event.isMiddleDown(), event.isRightDown());
	mCamera = mMayaCam.getCamera();
}

void ReymentaAudioVisualizerApp::mouseUp(MouseEvent event)
{
	// handle mouse up
	mMouseUpTime = getElapsedSeconds();
	mIsMouseDown = false;
}
// -------- SPOUT -------------
void ReymentaAudioVisualizerApp::shutdown()
{
    spoutsender.ReleaseSender();
}
// This line tells Cinder to actually create the application
CINDER_APP_NATIVE( ReymentaAudioVisualizerApp, RendererGl )
