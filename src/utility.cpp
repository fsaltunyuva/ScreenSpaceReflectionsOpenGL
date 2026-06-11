#include "utility.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <glm/glm.hpp>

#include <cstdio>
#include <cstdlib>
#include <bit>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <charconv>
#include <array>
#include <sstream>
#include <iostream>

void SetupGLFWErrorCallback();
void SetupOpenGLErrorCallback();
void APIENTRY PrintOpenGLError(GLenum, GLenum, GLuint, GLenum,
                               GLsizei, const GLchar*, const void*);

void SetupGLFWCallbacks(GLFWwindow* w, const CallbackPointersGLFW& cb)
{
    if(cb.mMoveCallback) glfwSetCursorPosCallback(w, cb.mMoveCallback);
    if(cb.mButtonCallback) glfwSetMouseButtonCallback(w, cb.mButtonCallback);
    if(cb.mScrollCallback) glfwSetScrollCallback(w, cb.mScrollCallback);
    if(cb.fboCallback) glfwSetFramebufferSizeCallback(w, cb.fboCallback);
    if(cb.keyCallback) glfwSetKeyCallback(w, cb.keyCallback);
    if(cb.winPosCallback) glfwSetWindowPosCallback(w, cb.winPosCallback);
    if(cb.winSizeCallback) glfwSetWindowSizeCallback(w, cb.winSizeCallback);
}

GLState::GLState(const char* const windowName,
                 int w, int h,
                 CallbackPointersGLFW callbacks)
    : curWndParams
        {
            .pos    = glm::ivec2(0),
            .size   = glm::ivec2(0),            
            .fbSize = glm::ivec2(w, h)
        }
{
    if(!glfwInit())
    {
        const char* err; glfwGetError(&err);
        std::fprintf(stderr,
                     "Unable to init GLFW!\n"
                     "Reason: %s\n"
                     "Terminating...\n",
                     err);
        std::exit(EXIT_FAILURE);
    }
    SetupGLFWErrorCallback();

    // Misc.
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GL_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_TRUE);
    glfwWindowHint(GLFW_REFRESH_RATE, GLFW_DONT_CARE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    // OGL Context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    // Set Debug Context for error reporting
    // Hopefully it will have minimal performance impact
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    //
    // Render buffer
    glfwWindowHint(GLFW_RED_BITS, 8); glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);glfwWindowHint(GLFW_ALPHA_BITS, 8);
    // Depth buffer
    glfwWindowHint(GLFW_DEPTH_BITS, 24); glfwWindowHint(GLFW_STENCIL_BITS, 8);
    //

    window = glfwCreateWindow(int(w), int(h), windowName, NULL, NULL);
    if(!window)
    {
        // Since the callback is set it should've printed the error
        // terminate the system
        glfwTerminate();
        std::exit(EXIT_FAILURE);
    }
    glfwGetWindowPos(window, &curWndParams.pos[0], &curWndParams.pos[1]);
    glfwGetWindowSize(window, &curWndParams.size[0], &curWndParams.size[1]);

    // Register callbacks
    SetupGLFWCallbacks(window, callbacks);
    // Give this class as pointer to the windowing system
    // This is safe since GLState cannot be move/copied etc.
    glfwSetWindowUserPointer(window, this);

    // Make the OGL context current for this window
    // After this call, all OGL APIs will act on this window
    glfwMakeContextCurrent(window);

    // Force vsync
    glfwSwapInterval(1);

    // Now we can load the OGL functions, function that loads OGL
    // functions is
    if(!gladLoadGL())
    {
        std::fprintf(stderr, "Unable to load OpenGL functions!");
        std::exit(EXIT_FAILURE);
    }

    // Now setup the OGL error callback
    SetupOpenGLErrorCallback();

    // Print OGL Status, this may be important.
    // When on laptop window may be using integrated GPU or
    // wrong version of OpenGL generated etc.
    std::printf("Window Initialized.\n");
    std::printf("GLFW   : %s\n", glfwGetVersionString());
    std::printf("\n");
    std::printf("Renderer Information...\n");
    std::printf("OpenGL : %s\n", glGetString(GL_VERSION));
    std::printf("GLSL   : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    std::printf("Device : %s\n", glGetString(GL_RENDERER));
    std::printf("\n");

    // Create shader pipeline
    glGenProgramPipelines(1, &renderPipeline);
    glBindProgramPipeline(renderPipeline);

    // All done! Happy rendering.
}

GLState::~GLState()
{
    if(renderPipeline) glDeleteProgramPipelines(1, &renderPipeline);
    if(window) glfwDestroyWindow(window);
    glfwTerminate();
}

ShaderGL::ShaderGL(Type t, const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if(!file)
    {
        std::printf("Unable to open shader file at \"%s\".\n",
                    path.c_str());
        std::exit(EXIT_FAILURE);
    }
    GLint sourceSize = GLint(file.seekg(0, std::ios::end).tellg());
    std::vector<GLchar> source(size_t(sourceSize + 1), '\0');
    file.seekg(0, std::ios::beg);
    file.read(source.data(), sourceSize);

    // Create temporary shader
    GLuint shaderGL = glCreateShader(t);
    GLchar* srcPtr = source.data();
    glShaderSource(shaderGL, 1, &srcPtr, &sourceSize);
    glCompileShader(shaderGL);
    GLint isCompiled = GL_FALSE;
    glGetShaderiv(shaderGL, GL_COMPILE_STATUS, &isCompiled);
    if(isCompiled == GL_FALSE)
    {
        // We do not need to print the compilation error
        // OpenGL debug callback will automatically handle it
        std::fprintf(stderr, "Unable to compile shader \"%s\"\n",
                     path.c_str());

        GLint errLen = 0;
        glGetShaderiv(shaderGL, GL_INFO_LOG_LENGTH, &errLen);
        std::vector<char> errLog(size_t(errLen + 1), '\0');
        glGetShaderInfoLog(shaderGL, errLen, &errLen, errLog.data());

        // Use our own print here
        PrintOpenGLError(GL_DEBUG_SOURCE_SHADER_COMPILER,
                         GL_DEBUG_TYPE_ERROR, 0,
                         GL_DEBUG_SEVERITY_HIGH,
                         errLen, errLog.data(), nullptr);

        std::exit(EXIT_FAILURE);
    }

    // Attach this to openGL "program"
    // (which represents entirity of the programmable rasterizer pipeline)
    shaderId = glCreateProgram();
    glProgramParameteri(shaderId, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(shaderId, shaderGL);
    glLinkProgram(shaderId);
    GLint isLinked = GL_FALSE;
    glGetProgramiv(shaderId, GL_LINK_STATUS, &isLinked);
    if(isLinked == GL_FALSE)
    {
        // We do not need to print the compilation error
        // OpenGL debug callback will automatically handle it
        std::fprintf(stderr, "Unable to link shader \"%s\"\n",
                     path.c_str());

        GLint errLen = 0;
        glGetProgramiv(shaderId, GL_INFO_LOG_LENGTH, &errLen);
        std::vector<char> errLog(size_t(errLen + 1), '\0');
        glGetProgramInfoLog(shaderId, errLen, &errLen, errLog.data());
        // Use our own print here
        PrintOpenGLError(GL_DEBUG_SOURCE_SHADER_COMPILER,
                         GL_DEBUG_TYPE_ERROR, 0,
                         GL_DEBUG_SEVERITY_HIGH,
                         errLen, errLog.data(), nullptr);
        std::exit(EXIT_FAILURE);
    }
    // After linking, we can detach the shader.
    // Actual compiled assembly will be stayed inside the pipeline (program).
    glDetachShader(shaderId, shaderGL);
    glDeleteShader(shaderGL);

    static const char* const VertexStr      = "Vertex";
    static const char* const FragmentStr    = "Fragment";
    const char* shaderTypeStr = nullptr;
    switch(t)
    {
        case ShaderGL::VERTEX:      shaderTypeStr = VertexStr; break;
        case ShaderGL::FRAGMENT:    shaderTypeStr = FragmentStr; break;
        default:
        {
            std::fprintf(stderr, "Unkown Shader Type while compiling \"%s\"!",
                         path.c_str());
            std::exit(EXIT_FAILURE);
        }
    }
    std::printf("%s Shader \"%s\" is compiled succesfully.\n",
                shaderTypeStr, path.c_str());
}

// For mesh multiple index hashing
struct ObjKeyType
{
    uint32_t posIndex;
    uint32_t uvIndex;
    uint32_t normalIndex;

    //auto operator==(const ObjKeyType& other) const
    //{
    //    return (posIndex == other.posIndex &&
    //            uvIndex == other.uvIndex &&
    //            normalIndex == other.normalIndex);
    //}

    auto operator<=>(const ObjKeyType&) const = default;
};

template<>
struct std::hash<ObjKeyType>
{
    std::uint64_t operator()(const ObjKeyType& k) const
    {
        return (k.posIndex * 7741ull +
                k.normalIndex * 5113ull +
                k.uvIndex * 9157ull);
    }
};

MeshGL::MeshGL(const std::string& objPath)
{
    std::ifstream file(objPath);
    if(!file)
    {
        std::fprintf(stderr, "Unable to open obj file \"%s\"\n", objPath.c_str());
        std::exit(EXIT_FAILURE);
    }

    std::vector<glm::vec3> temp_positions;
    std::vector<glm::vec2> temp_uvs;
    std::vector<glm::vec3> temp_normals;

    struct FaceVertex {
        int v = -1;
        int vt = -1;
        int vn = -1;
    };
    std::vector<std::vector<FaceVertex>> faces;

    std::string line;
    while(std::getline(file, line))
    {
        // Strip comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        // Trim leading spaces
        size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
        if (firstNonSpace == std::string::npos) continue;
        line = line.substr(firstNonSpace);

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            if (iss >> pos.x >> pos.y >> pos.z) {
                temp_positions.push_back(pos);
            }
        }
        else if (prefix == "vt") {
            glm::vec2 uv;
            if (iss >> uv.x >> uv.y) {
                temp_uvs.push_back(uv);
            }
        }
        else if (prefix == "vn") {
            glm::vec3 normal;
            if (iss >> normal.x >> normal.y >> normal.z) {
                temp_normals.push_back(normal);
            }
        }
        else if (prefix == "f") {
            std::vector<FaceVertex> faceVerts;
            std::string vertexStr;
            while (iss >> vertexStr) {
                FaceVertex fv;
                size_t firstSlash = vertexStr.find('/');
                if (firstSlash == std::string::npos) {
                    fv.v = std::stoi(vertexStr);
                } else {
                    fv.v = std::stoi(vertexStr.substr(0, firstSlash));
                    size_t secondSlash = vertexStr.find('/', firstSlash + 1);
                    if (secondSlash == std::string::npos) {
                        fv.vt = std::stoi(vertexStr.substr(firstSlash + 1));
                    } else {
                        if (secondSlash > firstSlash + 1) {
                            fv.vt = std::stoi(vertexStr.substr(firstSlash + 1, secondSlash - firstSlash - 1));
                        }
                        fv.vn = std::stoi(vertexStr.substr(secondSlash + 1));
                    }
                }
                
                // Convert 1-based to 0-based indices
                if (fv.v > 0) fv.v--;
                else if (fv.v < 0) fv.v = int(temp_positions.size()) + fv.v;

                if (fv.vt > 0) fv.vt--;
                else if (fv.vt < 0) fv.vt = int(temp_uvs.size()) + fv.vt;

                if (fv.vn > 0) fv.vn--;
                else if (fv.vn < 0) fv.vn = int(temp_normals.size()) + fv.vn;

                faceVerts.push_back(fv);
            }
            if (faceVerts.size() >= 3) {
                faces.push_back(faceVerts);
            }
        }
    }
    file.close();

    // Generate smooth normals if they are missing
    if (temp_normals.empty()) {
        std::cout << "No normals found in OBJ. Generating smooth normals instead." << std::endl;
        temp_normals.assign(temp_positions.size(), glm::vec3(0.0f));
        for (auto& face : faces) {
            for (size_t i = 1; i < face.size() - 1; ++i) {
                int i0 = face[0].v;
                int i1 = face[i].v;
                int i2 = face[i + 1].v;
                
                glm::vec3 p0 = temp_positions[i0];
                glm::vec3 p1 = temp_positions[i1];
                glm::vec3 p2 = temp_positions[i2];
                
                glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
                temp_normals[i0] += n;
                temp_normals[i1] += n;
                temp_normals[i2] += n;
            }
        }
        for (auto& n : temp_normals) {
            if (glm::length(n) > 0.0001f) {
                n = glm::normalize(n);
            } else {
                n = glm::vec3(0, 1, 0);
            }
        }
        for (auto& face : faces) {
            for (auto& fv : face) {
                fv.vn = fv.v;
            }
        }
    }

    // Convert data to single indexed buffer (triangulating polygons/quads)
    std::vector<glm::vec3> linPositions;
    std::vector<glm::vec3> linNormals;
    std::vector<glm::vec2> linUVs;
    std::vector<uint32_t> indices;
    
    std::unordered_map<ObjKeyType, uint32_t> indexHashes;
    uint32_t indexCounter = 0;

    auto ProcessVertex = [&](const FaceVertex& fv) -> uint32_t {
        ObjKeyType key = { (uint32_t)fv.v, (uint32_t)fv.vt, (uint32_t)fv.vn };
        auto it = indexHashes.find(key);
        if (it != indexHashes.end()) {
            return it->second;
        }
        
        uint32_t newIdx = indexCounter++;
        indexHashes[key] = newIdx;
        
        linPositions.push_back(temp_positions[fv.v]);
        
        if (fv.vn >= 0 && fv.vn < int(temp_normals.size())) {
            linNormals.push_back(temp_normals[fv.vn]);
        } else {
            linNormals.push_back(glm::vec3(0, 1, 0));
        }
        
        if (fv.vt >= 0 && fv.vt < int(temp_uvs.size())) {
            linUVs.push_back(temp_uvs[fv.vt]);
        } else {
            linUVs.push_back(glm::vec2(0, 0));
        }
        
        return newIdx;
    };

    for (const auto& face : faces) {
        for (size_t i = 1; i < face.size() - 1; ++i) {
            indices.push_back(ProcessVertex(face[0]));
            indices.push_back(ProcessVertex(face[i]));
            indices.push_back(ProcessVertex(face[i + 1]));
        }
    }

    std::array<size_t, 3> sizes = {};
    sizes[0] = linPositions.size() * sizeof(glm::vec3);
    sizes[1] = linNormals.size() * sizeof(glm::vec3);
    sizes[2] = linUVs.size() * sizeof(glm::vec2);

    std::array<size_t, 4> offsets = {};
    offsets[0] = 0;
    for(uint32_t i = 1; i < 4; i++)
    {
        size_t alignedSize = (sizes[i - 1] + 255) / 256 * 256;
        offsets[i] = offsets[i - 1] + alignedSize;
    }

    glGenBuffers(1, &vBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, vBufferId);
    glBufferStorage(GL_ARRAY_BUFFER, GLintptr(offsets.back()), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[0]), GLsizei(sizes[0]), linPositions.data());
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[1]), GLsizei(sizes[1]), linNormals.data());
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[2]), GLsizei(sizes[2]), linUVs.data());
    
    glGenBuffers(1, &iBufferId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iBufferId);
    glBufferStorage(GL_ELEMENT_ARRAY_BUFFER, GLsizei(indices.size() * sizeof(uint32_t)),
                    indices.data(), GL_DYNAMIC_STORAGE_BIT);

    glGenVertexArrays(1, &vaoId);
    glBindVertexArray(vaoId);
    glBindVertexBuffer(0, vBufferId, GLintptr(offsets[0]), GLsizei(sizeof(glm::vec3)));
    glEnableVertexAttribArray(0);
    glVertexAttribFormat(0, 3, GL_FLOAT, false, 0);
    glBindVertexBuffer(1, vBufferId, GLintptr(offsets[1]), GLsizei(sizeof(glm::vec3)));
    glEnableVertexAttribArray(1);
    glVertexAttribFormat(1, 3, GL_FLOAT, false, 0);
    glBindVertexBuffer(2, vBufferId, GLintptr(offsets[2]), GLsizei(sizeof(glm::vec2)));
    glEnableVertexAttribArray(2);
    glVertexAttribFormat(2, 2, GL_FLOAT, false, 0);

    glVertexAttribBinding(0, IN_POS);
    glVertexAttribBinding(1, IN_NORMAL);
    glVertexAttribBinding(2, IN_UV);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iBufferId);
    glBindVertexArray(0);

    std::printf("Obj file \"%s\" is loaded successfully.\n", objPath.c_str());

    indexCount = uint32_t(indices.size());
    assert(indexCount % 3 == 0);
}

TextureGL::TextureGL(const std::string& texPath,
                     SampleMode sampleMode, EdgeResolve edgeResolveMode)
{
    stbi_set_flip_vertically_on_load(1);
    std::FILE* f = fopen(texPath.c_str(), "rb");
    if(!f)
    {
        std::fprintf(stderr, "Unable to open image \"%s\"\n", texPath.c_str());
        std::exit(EXIT_FAILURE);
    }
    //
    bool isHDR = stbi_is_hdr(texPath.c_str());
    bool is16Bit = !isHDR && stbi_is_16_bit_from_file(f);
    void* rawPixels = nullptr;
    
    if(isHDR)   rawPixels = stbi_loadf_from_file(f, &width, &height, &channelCount, 0);
    else if(is16Bit) rawPixels = stbi_load_from_file_16(f, &width, &height, &channelCount, 0);
    else        rawPixels = stbi_load_from_file(f, &width, &height, &channelCount, 0);
    
    fclose(f); 
    //
    if(!rawPixels)
    {
        std::fprintf(stderr, "Unable to read image \"%s\"\n", texPath.c_str());
        std::exit(EXIT_FAILURE);
    }

    // Mipmap count calculation
    uint32_t mipCount = uint32_t(std::max(width, height));
    mipCount = (sizeof(GLsizei) * CHAR_BIT) - uint32_t(std::countl_zero(mipCount));
    //
    GLenum internalFormatSized = 0;
    GLenum internalFormat = 0;
    GLenum pixType = GL_UNSIGNED_BYTE;
    if (isHDR) pixType = GL_FLOAT;
    else if (is16Bit) pixType = GL_UNSIGNED_SHORT;

    switch(channelCount)
    {
        case 1: internalFormatSized = isHDR ? GL_R32F : (is16Bit ? GL_R16    : GL_R8);
                internalFormat      = GL_RED;
                break;
        case 2: internalFormatSized = isHDR ? GL_RG32F : (is16Bit ? GL_RG16   : GL_RG8);
                internalFormat      = GL_RG;
                break;
        case 3: internalFormatSized = isHDR ? GL_RGB32F : (is16Bit ? GL_RGB16  : GL_RGB8);
                internalFormat      = GL_RGB;
                break;
        case 4: internalFormatSized = isHDR ? GL_RGBA32F : (is16Bit ? GL_RGBA16 : GL_RGBA8);
                internalFormat      = GL_RGBA;
                break;
        default:
        {
            stbi_image_free(rawPixels);
            std::fprintf(stderr, "Unkown image type!\n");
            std::exit(EXIT_FAILURE);
        }
    }

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexStorage2D(GL_TEXTURE_2D, GLsizei(mipCount), internalFormatSized, width, height);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, internalFormat,
                    pixType, rawPixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, edgeResolveMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, edgeResolveMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampleMode);
    if(sampleMode == NEAREST)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(rawPixels);
}

GeoDataDTED::GeoDataDTED(const std::string& fName)
{
    // Helper Functions
    static constexpr double MINUTE_FACTOR = double(1) / double(60);
    static constexpr double SECOND_FACTOR = MINUTE_FACTOR * (double(1) / double(60));
    static auto ASCIIToInt = [](std::byte numChar)
    {
        return uint32_t(numChar) - 48;
    };
    static auto ParseLatLon = [](std::ifstream& file)
    {
        std::array<std::byte, 8> encoded;
        file.read(reinterpret_cast<char*>(encoded.data()), 8);
        //
        uint32_t degrees = (ASCIIToInt(encoded[2]) * 1 +
                            ASCIIToInt(encoded[1]) * 10 +
                            ASCIIToInt(encoded[0]) * 100);
        uint32_t minutes = (ASCIIToInt(encoded[4]) * 1 +
                            ASCIIToInt(encoded[3]) * 10);
        uint32_t seconds = (ASCIIToInt(encoded[6]) * 1 +
                            ASCIIToInt(encoded[5]) * 10);
        // Add values in inverse order from small to large to get somewhat
        // higher precision maybe?
        double result = 0.0;
        result += double(seconds) * SECOND_FACTOR;
        result += double(minutes) * MINUTE_FACTOR;
        result += double(degrees);

        char hemisphere = char(encoded[7]);
        if(hemisphere == 'S' || hemisphere == 'W')
            result *= double(-1);
        return result;
    };
    static auto ReadBCD4Digit = [](std::ifstream& file)
    {
        std::array<std::byte, 4> encoded;
        file.read(reinterpret_cast<char*>(encoded.data()), 4);
        uint32_t result = (ASCIIToInt(encoded[3]) * 1 +
                           ASCIIToInt(encoded[2]) * 10 +
                           ASCIIToInt(encoded[1]) * 100 +
                           ASCIIToInt(encoded[0]) * 1000);
        return result;
    };

    std::ifstream file(fName, std::ios::binary);
    if(!file)
    {
        std::fprintf(stderr, "Unable to open DTED file \"%s\"\n",
                     fName.c_str());
        std::exit(EXIT_FAILURE);
    }

    // Implementation of the specification below
    // https://everyspec.com/MIL-PRF/MIL-PRF-080000-99999/MIL-PRF-89020B_25316/
    //
    // Section 3.9.2
    // DTED file consists of
    //  - User Header Label (UHL)
    //  - Data Set Identification Label (DSI)
    //  - Accuracy Description Record (ACC)
    //  - Series of data records
    //
    // Data records are 16-bit integer in meters in BIG ENDIAN format
    //
    // Section 3.12.c | User Header Label (UHL)
    std::array<char, 4> fourCC;
    file.read(fourCC.data(), 4); fourCC.back() = '\0';
    if(std::strncmp(fourCC.data(), "UHL", 3))
    {
        std::fprintf(stderr, "Wrong FOURCC Code in DTED file \"%s\". "
                     "Was %s, should be \"UHL\"\n",
                     fName.c_str(), fourCC.data());
        std::exit(EXIT_FAILURE);
    }
    // Next 8 bytes is bottom left (South West) longitude
    // It has a binary coded decimal structure.
    lonRange[0] = ParseLatLon(file); // First is Lon
    latRange[0] = ParseLatLon(file); // Second is Lat
    double lonInterval = double(ReadBCD4Digit(file)) * SECOND_FACTOR;
    double latInterval = double(ReadBCD4Digit(file)) * SECOND_FACTOR;
    // Read count parameters
    static constexpr size_t LAT_LON_COUNT_START = 47;
    file.seekg(LAT_LON_COUNT_START);
    dimensions[1] = ReadBCD4Digit(file);
    dimensions[0] = ReadBCD4Digit(file);
    // Finalize the extents in (lon,lat)
    latRange[1] = latRange[0] + double(dimensions[0] - 1) * latInterval;
    lonRange[1] = lonRange[0] + double(dimensions[1] - 1) * lonInterval;
    // We don't care about the rest on UHL
    // Section 3.12.d | Data Set Idendtification Record (DSI)
    // We completely skip this
    // Section 3.12.e | Accuracy Description Record (ACC)
    // We completely skip this
    static constexpr size_t DATA_RECORD_START_OFFSET = 3428;
    file.seekg(DATA_RECORD_START_OFFSET);

    // Section 3.12.f | Data Record
    // Data is recorded as scanlines, each scanline has its own record
    minMax[0] = FLT_MAX;
    minMax[1] = -FLT_MAX;
    uint32_t totalPoints = dimensions[0] * dimensions[1];
    heightValues.resize(totalPoints);
    //
    for(uint32_t y = 0; y < dimensions[1]; y++)
    {
        // TODO: Bulk read each scanline then do the endianness convert
        static constexpr std::byte DATA_RECORD_SENTINEL = std::byte(170); // 252 in octal
        char sentinel; file.read(&sentinel, 1);
        if(std::byte(sentinel) != DATA_RECORD_SENTINEL)
        {
            std::fprintf(stderr, "Wrong data sentinel in DTED file \"%s\", "
                         "scanline: %d. Was %d, should be %d.\n",
                         fName.c_str(), y, int(sentinel),
                         int(DATA_RECORD_SENTINEL));
            std::exit(EXIT_FAILURE);
        }
        // Skip local lat/lon counts (it was all zero on the file that I've tested)
        // But we need to add these to checksum afaik.
        std::byte sectionHeader[7];  file.read(reinterpret_cast<char*>(sectionHeader), 7);
        uint32_t checksum = (uint32_t(sectionHeader[0]) + uint32_t(sectionHeader[1]) +
                             uint32_t(sectionHeader[2]) + uint32_t(sectionHeader[3]) +
                             uint32_t(sectionHeader[4]) + uint32_t(sectionHeader[5]) +
                             uint32_t(sectionHeader[6]));
        checksum += uint32_t(DATA_RECORD_SENTINEL);

        for(uint32_t x = 0; x < dimensions[0]; x++)
        {
            std::byte data[2]; file.read(reinterpret_cast<char*>(data), 2);
            checksum += uint32_t(data[0]) + uint32_t(data[1]);
            uint16_t elevUInt = (uint16_t(data[0]) << uint16_t(8)) | uint16_t(data[1]);

            // Data is 1s-complement?
            bool isNeg = (elevUInt & 0x8000) == 0x8000;
            elevUInt = elevUInt & 0x7FFF;
            //
            int16_t elev = (isNeg) ? int16_t(elevUInt) * int16_t(-1) : int16_t(elevUInt);
            float elevFloat = float(elev);
            heightValues[y * dimensions[0] + x] = elevFloat;
            minMax[0] = std::min(minMax[0], elevFloat);
            minMax[1] = std::max(minMax[1], elevFloat);
        }
        // Read the checksum
        std::byte checksumData[4]; file.read(reinterpret_cast<char*>(checksumData), 4);
        uint32_t checksumFile = 0;
        checksumFile |= uint32_t(checksumData[0]) << 24u;
        checksumFile |= uint32_t(checksumData[1]) << 16u;
        checksumFile |= uint32_t(checksumData[2]) <<  8u;
        checksumFile |= uint32_t(checksumData[3]) <<  0u;
        if(checksumFile != checksum)
        {
            std::fprintf(stderr, "Wrong checksum in DTED file \"%s\", "
                         "scanline %d. Was %d, should be %d.\n",
                         fName.c_str(), y, checksum, checksumFile);
            std::exit(EXIT_FAILURE);
        }
    }
    // Finally done...
}

void SetupGLFWErrorCallback()
{
    // Local function as lambda, should not capture anything
    // for it to be usable with C API of GLFW.
    static auto PrintErr = [](int errorCode, const char* err)
    {
        std::fprintf(stderr, "[ERR][GLFW]:[%d]: \"%s\"\n", errorCode, err);
    };
    //
    glfwSetErrorCallback(PrintErr);
}

void APIENTRY PrintOpenGLError(GLenum source, GLenum type,
                               GLuint id, GLenum severity,
                               GLsizei, const GLchar* message,
                               const void*)
{
    const char* srcStr = nullptr;
    switch(source)
    {
        case GL_DEBUG_SOURCE_API:               srcStr = "API";             break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:     srcStr = "WINDOW";          break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER:   srcStr = "SHADER_COMPILER"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:       srcStr = "THIRD_PARTY";     break;
        case GL_DEBUG_SOURCE_APPLICATION:       srcStr = "APPLICATION";     break;
        case GL_DEBUG_SOURCE_OTHER:             srcStr = "OTHER";           break;
        default:                                srcStr = "UNKNOWN";         break;
    }
    //
    const char* errStr = nullptr;
	switch(type)
	{
		case GL_DEBUG_TYPE_ERROR:               errStr = "ERROR";               break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: errStr = "DEPRECATED_BEHAVIOR"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  errStr = "UNDEFINED_BEHAVIOR";  break;
		case GL_DEBUG_TYPE_PORTABILITY:         errStr = "PORTABILITY";         break;
		case GL_DEBUG_TYPE_PERFORMANCE:         errStr = "PERFORMANCE";         break;
        case GL_DEBUG_TYPE_OTHER:               errStr = "OTHER";               break;
        default:                                errStr = "UNKNOWN";             break;
	}

    // Do not print if debug type is other, it prints information and
    // may be mistaken as error
    if(type == GL_DEBUG_TYPE_OTHER) return;

    //
    const char* severityStr = nullptr;
	switch(severity)
	{
		case GL_DEBUG_SEVERITY_LOW:     severityStr = "LOW";     break;
		case GL_DEBUG_SEVERITY_MEDIUM:  severityStr = "MEDIUM";  break;
		case GL_DEBUG_SEVERITY_HIGH:    severityStr = "HIGH";    break;
		default:                        severityStr = "UNKNOWN"; break;
	}

    std::fprintf(stderr,
                 "======== OGL-INFO ========\n"
                 "[%d]:[%s]:[%s]:[%s]\n"
                 "%s\n"
                 "===========================\n",
                 id, srcStr, errStr, severityStr, message);
};

void SetupOpenGLErrorCallback()
{
    // Add Callback
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(PrintOpenGLError, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                          GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
}
