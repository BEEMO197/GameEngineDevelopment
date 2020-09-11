/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreGpuProgram.h"
#include "OgreRoot.h"
#include "OgreRenderSystem.h"
#include "OgreRenderSystemCapabilities.h"
#include "OgreStringConverter.h"
#include "OgreGpuProgramManager.h"
#include "OgreHighLevelGpuProgramManager.h"
#include "OgreLogManager.h"

#include "OgreGLSLShaderCommon.h"
#include "OgreGLSLPreprocessor.h"

namespace Ogre {
    //-----------------------------------------------------------------------
    uint GLSLShaderCommon::mShaderCount = 0;
    GLSLShaderCommon::CmdPreprocessorDefines GLSLShaderCommon::msCmdPreprocessorDefines;
    GLSLShaderCommon::CmdAttach GLSLShaderCommon::msCmdAttach;
    GLSLShaderCommon::CmdColumnMajorMatrices GLSLShaderCommon::msCmdColumnMajorMatrices;

    String GLSLShaderCommon::getResourceLogName() const
    {
        if(mLoadFromFile)
            return "'" + mFilename + "'";
        return "'"+mName+"'";
    }

    //-----------------------------------------------------------------------
    void GLSLShaderCommon::loadFromSource(void)
    {
        // Preprocess the GLSL shader in order to get a clean source
        CPreprocessor cpp;

        // Define "predefined" macros.
        // TODO: decide, should we define __VERSION__, and with what value.
        if(getLanguage() == "glsles")
            cpp.Define("GL_ES", 5, 1);

        String defines = mPreprocessorDefines;
        for(const auto& def : parseDefines(defines))
        {
            cpp.Define(def.first, strlen(def.first), def.second, strlen(def.second));
        }

		// deal with includes
		mSource = _resolveIncludes(mSource, this, mFilename);

        size_t out_size = 0;
        const char *src = mSource.c_str ();
        size_t src_len = mSource.size ();
        char *out = cpp.Parse (src, src_len, out_size);
        if (!out || !out_size)
            // Failed to preprocess, break out
            OGRE_EXCEPT(Exception::ERR_RENDERINGAPI_ERROR, "Failed to preprocess shader " + mName);

        mSource = String (out, out_size);
        if (out < src || out > src + src_len)
            free (out);

        compile(true);
    }
    //---------------------------------------------------------------------------
    void GLSLShaderCommon::unloadImpl()
    {
        // We didn't create mAssemblerProgram through a manager, so override this
        // implementation so that we don't try to remove it from one. Since getCreator()
        // is used, it might target a different matching handle!
        mAssemblerProgram.reset();

        unloadHighLevel();
    }

    //-----------------------------------------------------------------------
    void GLSLShaderCommon::populateParameterNames(GpuProgramParametersSharedPtr params)
    {
        getConstantDefinitions();
        params->_setNamedConstants(mConstantDefs);
        // Don't set logical / physical maps here, as we can't access parameters by logical index in GLHL.
    }
    //-----------------------------------------------------------------------
    GLSLShaderCommon::GLSLShaderCommon(ResourceManager* creator, 
        const String& name, ResourceHandle handle,
        const String& group, bool isManual, ManualResourceLoader* loader)
        : HighLevelGpuProgram(creator, name, handle, group, isManual, loader)
        , mColumnMajorMatrices(true)
        , mLinked(0)
        , mShaderID(++mShaderCount) // Increase shader counter and use as ID
    {
    }
    //-----------------------------------------------------------------------
    String GLSLShaderCommon::CmdAttach::doGet(const void *target) const
    {
        return (static_cast<const GLSLShaderCommon*>(target))->getAttachedShaderNames();
    }
    //-----------------------------------------------------------------------
    void GLSLShaderCommon::CmdAttach::doSet(void *target, const String& shaderNames)
    {
        //get all the shader program names: there could be more than one
        StringVector vecShaderNames = StringUtil::split(shaderNames, " \t", 0);

        size_t programNameCount = vecShaderNames.size();
        for ( size_t i = 0; i < programNameCount; ++i )
        {
            static_cast<GLSLShaderCommon*>(target)->attachChildShader(vecShaderNames[i]);
        }
    }
    //-----------------------------------------------------------------------
    String GLSLShaderCommon::CmdPreprocessorDefines::doGet(const void *target) const
    {
        return static_cast<const GLSLShaderCommon*>(target)->getPreprocessorDefines();
    }
    void GLSLShaderCommon::CmdPreprocessorDefines::doSet(void *target, const String& val)
    {
        static_cast<GLSLShaderCommon*>(target)->setPreprocessorDefines(val);
    }

    //-----------------------------------------------------------------------
    void GLSLShaderCommon::attachChildShader(const String& name)
    {
        // is the name valid and already loaded?
        // check with the high level program manager to see if it was loaded
        HighLevelGpuProgramPtr hlProgram = HighLevelGpuProgramManager::getSingleton().getByName(
            name, ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
        if (hlProgram && hlProgram->getSyntaxCode() == "glsl")
        {
            // make sure attached program source gets loaded and compiled
            // don't need a low level implementation for attached shader objects
            // loadHighLevel will only load the source and compile once
            // so don't worry about calling it several times
            GLSLShaderCommon* childShader = static_cast<GLSLShaderCommon*>(hlProgram.get());
            // load the source and attach the child shader only if supported
            if (isSupported())
            {
                childShader->loadHighLevel();
                // add to the container
                mAttachedGLSLPrograms.push_back( childShader );
                mAttachedShaderNames += name + " ";
            }
        }
    }
    //-----------------------------------------------------------------------
    String GLSLShaderCommon::CmdColumnMajorMatrices::doGet(const void *target) const
    {
        return StringConverter::toString(static_cast<const GLSLShaderCommon*>(target)->getColumnMajorMatrices());
    }
    void GLSLShaderCommon::CmdColumnMajorMatrices::doSet(void *target, const String& val)
    {
        static_cast<GLSLShaderCommon*>(target)->setColumnMajorMatrices(StringConverter::parseBool(val));
    }
}