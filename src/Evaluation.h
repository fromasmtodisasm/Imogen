#pragma once
#include <string>
#include <vector>
#include <map>

typedef unsigned int TextureID;

inline void TexParam(TextureID MinFilter, TextureID MagFilter, TextureID WrapS, TextureID WrapT, TextureID texMode)
{
	glTexParameteri(texMode, GL_TEXTURE_MIN_FILTER, MinFilter);
	glTexParameteri(texMode, GL_TEXTURE_MAG_FILTER, MagFilter);
	glTexParameteri(texMode, GL_TEXTURE_WRAP_S, WrapS);
	glTexParameteri(texMode, GL_TEXTURE_WRAP_T, WrapT);
}

class RenderTarget
{

public:
	RenderTarget() : mGLTexID(0)
	{
		fbo = 0;
		depthbuffer = 0;
		mWidth = mHeight = 0;
	}

	void initBuffer(int width, int height, bool hasZBuffer);
	void bindAsTarget() const;

	void clear();

	TextureID txDepth;
	unsigned int mGLTexID;
	int mWidth, mHeight;
	TextureID fbo;
	TextureID depthbuffer;

	void destroy();

	void checkFBO();
};


// simple API
struct Evaluation
{
	Evaluation();

	void SetEvaluationGLSL(const std::vector<std::string>& filenames);
	std::string GetEvaluationGLSL(const std::string& filename);


	void LoadEquiRectHDREnvLight(const std::string& filepath);
	void LoadEquiRect(const std::string& filepath);

	size_t AddEvaluationTarget();
	void DelEvaluationTarget(size_t target);
	unsigned int GetEvaluationTexture(size_t target);
	void SetEvaluationCall(size_t target, const std::string& shaderCall);
	void AddEvaluationInput(size_t target, int slot, int source);
	void DelEvaluationInput(size_t target, int slot);
	void RunEvaluation();
	void SetEvaluationOrder(const std::vector<size_t> nodeOrderList);
	void SetTargetDirty(size_t target);
	void Bake(const char *szFilename, size_t target, int width, int height);

protected:
	std::string mBaseShader;
	unsigned int equiRectTexture;
	int mDirtyCount;
	std::map<std::string, std::string> mGLSLs;

	struct Input
	{
		Input()
		{
			memset(mInputs, -1, sizeof(int) * 8);
		}
		int mInputs[8];
	};

	struct EvaluationStage
	{
		RenderTarget mTarget;
		unsigned int mShader;
		Input mInput;
		bool mbDirty;
	};

	std::vector<EvaluationStage> mEvaluations;
	std::vector<size_t> mEvaluationOrderList;
};