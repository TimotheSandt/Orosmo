#include "gl_device.hpp"

#include "gl_translate.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/platform/window.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace orosmo::rhi::gl
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

namespace
{

// Vide la file d'erreurs GL, pour qu'un test d'erreur ne lise que celles de l'appel qui suit.
void clearGlErrors()
{
    while (glGetError() != GL_NO_ERROR)
    {
    }
}

// Vrai si une des commandes GL depuis le dernier clearGlErrors a échoué (génération, liaison, stockage : n'importe quelle
// erreur, pas seulement le manque de mémoire). La file est vidée.
bool glFailed()
{
    bool failed = false;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
    {
        failed = true;
    }
    return failed;
}

void label(GLenum identifier, GLuint id, const std::string &name)
{
    if (!name.empty())
    {
        glObjectLabel(identifier, id, static_cast<GLsizei>(name.size()), name.c_str());
    }
}

bool hasExtension(const char *name)
{
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i)
    {
        const auto *ext = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext != nullptr && std::strcmp(ext, name) == 0)
        {
            return true;
        }
    }
    return false;
}

GLint getInt(GLenum pname)
{
    GLint value = 0;
    glGetIntegerv(pname, &value);
    return value;
}

GLint getIndexedInt(GLenum pname, GLuint index)
{
    GLint value = 0;
    glGetIntegeri_v(pname, index, &value);
    return value;
}

void APIENTRY debugCallback(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *user)
{
    auto *device = static_cast<GlDevice *>(const_cast<void *>(user));
    const bool isError = type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR ||
                         type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR;
    if (isError)
    {
        LOG_ERROR(1, "OpenGL validation: ", std::string(message, static_cast<std::size_t>(length)));
        device->onValidationMessage(true);
    }
    else if (severity != GL_DEBUG_SEVERITY_NOTIFICATION && device->firstInformationalMessage(id))
    {
        LOG_DEBUG("OpenGL: ", std::string(message, static_cast<std::size_t>(length)), " (message ", id,
                  " : journalisé une seule fois)");
    }
}

std::string programLog(GLuint program)
{
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    return log;
}

std::string shaderLog(GLuint shader)
{
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    return log;
}

} // namespace

void GlDevice::onValidationMessage(bool isError) noexcept
{
    if (isError)
    {
        validationErrors_.fetch_add(1);
    }
}

bool GlDevice::firstInformationalMessage(GLuint id)
{
    std::lock_guard<std::mutex> lock(informationalMutex_);
    return informationalSeen_.insert(id).second;
}

// ── Création du périphérique ───────────────────────────────────────────────────────────────────────────────────────

Result<std::unique_ptr<Device>> createGlDevice(const DeviceDesc &desc)
{
    auto device = GlDevice::create(desc);
    if (!device)
    {
        return std::unexpected(device.error());
    }
    return std::unique_ptr<Device>(std::move(*device));
}

Result<std::unique_ptr<GlDevice>> GlDevice::create(const DeviceDesc &desc)
{
    std::unique_ptr<GlDevice> device(new GlDevice());
    if (auto r = device->init(desc); !r)
    {
        return std::unexpected(r.error());
    }
    return device;
}

void applyWindowHints(const DeviceDesc &desc)
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, desc.validation ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE); // backbuffer RGBA8 linéaire : aucune conversion sRGB à l'écriture
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
}

Result<> GlDevice::init(const DeviceDesc &desc)
{
    windowed_ = desc.surface.nativeWindow != nullptr;
    framesInFlight_ = std::clamp(desc.framesInFlight, 1u, kMaxFramesInFlight); // createDevice a refusé les autres valeurs
    backbufferSize_ = {std::max(desc.surface.size.width, 1u), std::max(desc.surface.size.height, 1u)};

    if (windowed_)
    {
        window_ = static_cast<GLFWwindow *>(desc.surface.nativeWindow);
        ownsWindow_ = false;
    }
    else
    {
        // GLFW est initialisé ici mais jamais terminé : sa durée de vie appartient à `platform`.
        if (glfwInit() != GLFW_TRUE)
        {
            return makeError(ErrorCode::kUnsupported, "OpenGL RHI: glfwInit failed (no display?)");
        }
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        applyWindowHints(desc);
        window_ = glfwCreateWindow(1, 1, "rhi-offscreen", nullptr, nullptr);
        if (window_ == nullptr)
        {
            return makeError(ErrorCode::kUnsupported, "OpenGL RHI: cannot create an OpenGL 4.3 core context");
        }
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(desc.vsync && windowed_ ? 1 : 0);
    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
    {
        return makeError(ErrorCode::kUnsupported, "OpenGL RHI: gladLoadGLLoader failed");
    }
    if (GLVersion.major < 4 || (GLVersion.major == 4 && GLVersion.minor < 3))
    {
        return makeError(ErrorCode::kUnsupported, "OpenGL RHI: OpenGL 4.3 or newer is required");
    }
    // Repère du RHI (QUESTIONS A26) : origine en HAUT à gauche (la ligne 0 d'une cible est son haut) et profondeur dans [0, 1],
    // comme Vulkan et Metal. OpenGL l'obtient par glClipControl (cœur 4.5, ou GL_ARB_clip_control sur un pilote 4.3).
    if (glad_glClipControl == nullptr && hasExtension("GL_ARB_clip_control"))
    {
        glad_glClipControl = reinterpret_cast<PFNGLCLIPCONTROLPROC>(glfwGetProcAddress("glClipControl"));
    }
    if (glad_glClipControl == nullptr)
    {
        return makeError(ErrorCode::kUnsupported, "OpenGL RHI: glClipControl is required (OpenGL 4.5 or GL_ARB_clip_control): the RHI uses an "
                                                  "upper-left origin and a [0, 1] depth range");
    }
    glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
    // Tampons kUpload mappés en permanence (cœur 4.4, ou GL_ARB_buffer_storage sur un pilote 4.3) : `updateBuffer` y devient une
    // copie mémoire. Sans la fonction, repli sur glBufferData + glBufferSubData (le comportement d'avant). ENGINE_GL_BUFFER_STORAGE=0
    // force le repli (tests, comparaison de mesures).
    if (glad_glBufferStorage == nullptr && hasExtension("GL_ARB_buffer_storage"))
    {
        glad_glBufferStorage = reinterpret_cast<PFNGLBUFFERSTORAGEPROC>(glfwGetProcAddress("glBufferStorage"));
    }
    const char *storageOverride = std::getenv("ENGINE_GL_BUFFER_STORAGE");
    bufferStorage_ = glad_glBufferStorage != nullptr && (storageOverride == nullptr || std::strcmp(storageOverride, "0") != 0);
    glReady_ = true;

    if (desc.validation)
    {
        if ((getInt(GL_CONTEXT_FLAGS) & GL_CONTEXT_FLAG_DEBUG_BIT) != 0)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(debugCallback, this);
            debugCallbackInstalled_ = true;
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        }
        else
        {
            LOG_WARNING("OpenGL RHI: validation requested but the window's context is not a debug context (call prepareWindowHints first)");
        }
    }

    // ── Capacités ────────────────────────────────────────────────────────────────────────────────────────────────
    caps_.backend = Backend::kOpenGL;
    caps_.deviceName = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    caps_.driverVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    caps_.maxTextureSize = static_cast<std::uint32_t>(getInt(GL_MAX_TEXTURE_SIZE));
    caps_.max3DTextureSize = static_cast<std::uint32_t>(getInt(GL_MAX_3D_TEXTURE_SIZE));
    caps_.maxColorAttachments = std::min<std::uint32_t>(static_cast<std::uint32_t>(getInt(GL_MAX_COLOR_ATTACHMENTS)), kMaxAttachments);
    for (GLuint i = 0; i < 3; ++i)
    {
        caps_.maxComputeWorkGroupSize[i] = static_cast<std::uint32_t>(getIndexedInt(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i));
        caps_.maxComputeWorkGroupCount[i] = static_cast<std::uint32_t>(getIndexedInt(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i));
    }
    caps_.maxComputeInvocations = static_cast<std::uint32_t>(getInt(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS));
    caps_.minUniformAlignment = static_cast<std::uint64_t>(getInt(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT));
    caps_.minStorageAlignment = static_cast<std::uint64_t>(getInt(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT));
    caps_.maxUniformBufferSize = static_cast<std::uint64_t>(getInt(GL_MAX_UNIFORM_BLOCK_SIZE));
    caps_.maxPushConstantSize = 128;
    caps_.compute = true; // cœur de GL 4.3
    caps_.indirectDraw = true;
    caps_.multiDrawIndirect = true;
    caps_.drawIndirectFirstInstance = true; // baseInstance des commandes indirectes : cœur depuis GL 4.2
    caps_.timestampQueries = true;
    caps_.anisotropy = hasExtension("GL_EXT_texture_filter_anisotropic") || hasExtension("GL_ARB_texture_filter_anisotropic");
    caps_.bc7Textures = true; // BPTC : cœur d'OpenGL 4.2
    caps_.astcTextures = hasExtension("GL_KHR_texture_compression_astc_ldr");
    caps_.wireframe = true;
    caps_.srgbStorageImages = glFormatOf(Format::kRGBA8Srgb).imageCompatible; // faux : pas de format d'image sRGB en GLSL 4.30
    caps_.validation = debugCallbackInstalled_;
    caps_.spirvShaders = false; // GL_ARB_gl_spirv : non utilisé avant le lot B
    caps_.shaderFloat64 = true;  // cœur de GL 4.0 (GL_ARB_gpu_shader_fp64)
    maxImageUnits_ = getInt(GL_MAX_IMAGE_UNITS);

    // Tampons de stockage : OpenGL 4.3 n'en garantit que 8 ; la limite est exposée et vérifiée à la liaison (setBindGroup), un
    // périphérique qui n'en offre que 8 reste utilisable pour tout ce qui n'en lie pas au-delà.
    caps_.maxStorageBufferBindings = static_cast<std::uint32_t>(getInt(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS));
    if (getInt(GL_MAX_UNIFORM_BUFFER_BINDINGS) < static_cast<GLint>(kPushConstantBinding + 1) ||
        getInt(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS) < static_cast<GLint>(kScratchTextureUnit + 1))
    {
        return makeError(ErrorCode::kUnsupported, "OpenGL RHI: the driver offers too few uniform buffer or texture binding points");
    }

    // ── État global fixe ─────────────────────────────────────────────────────────────────────────────────────────
    glEnable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenFramebuffers(1, &scratchFbo_);
    glGenVertexArrays(1, &emptyVao_);
    glBindVertexArray(emptyVao_);

    glGenBuffers(1, &pushBuffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, pushBuffer_);
    glBufferData(GL_UNIFORM_BUFFER, caps_.maxPushConstantSize, nullptr, GL_DYNAMIC_DRAW);
    label(GL_BUFFER, pushBuffer_, "rhi.pushConstants");
    glBindBufferBase(GL_UNIFORM_BUFFER, kPushConstantBinding, pushBuffer_);

    if (windowed_)
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        backbufferSize_ = {static_cast<std::uint32_t>(std::max(w, 1)), static_cast<std::uint32_t>(std::max(h, 1))};
        TextureRes res;
        res.id = 0; // framebuffer par défaut
        res.target = GL_TEXTURE_2D;
        res.desc.format = Format::kRGBA8Unorm;
        res.desc.size = {backbufferSize_.width, backbufferSize_.height, 1};
        res.desc.usage = TextureUsage::kRenderTarget | TextureUsage::kCopySrc;
        res.desc.debugName = "rhi.backbuffer";
        res.defaultFramebuffer = true;
        backbuffer_ = textures_.add(std::move(res));
        return {};
    }
    TextureDesc backbufferDesc;
    backbufferDesc.format = Format::kRGBA8Unorm;
    backbufferDesc.size = {backbufferSize_.width, backbufferSize_.height, 1};
    backbufferDesc.usage = TextureUsage::kRenderTarget | TextureUsage::kSampled | TextureUsage::kCopySrc;
    backbufferDesc.debugName = "rhi.backbuffer";
    auto backbuffer = createTextureImpl(backbufferDesc);
    if (!backbuffer)
    {
        return std::unexpected(backbuffer.error());
    }
    backbuffer_ = *backbuffer;
    return {};
}

GlDevice::~GlDevice()
{
    if (window_ == nullptr)
    {
        return;
    }
    if (!glReady_)
    {
        if (ownsWindow_)
        {
            glfwDestroyWindow(window_);
        }
        return;
    }
    glfwMakeContextCurrent(window_);
    glFinish();
    flushReleases(true);
    for (const FrameFence &f : fences_)
    {
        glDeleteSync(f.sync);
    }
    fences_.clear();
    for (const UseFence &f : useFences_)
    {
        glDeleteSync(f.sync);
    }
    useFences_.clear();
    if (!ownsWindow_ && debugCallbackInstalled_)
    {
        // Le contexte retourne à son créateur : le rappel que CE périphérique a posé retient `this`, il ne doit pas lui survivre.
        // Un rappel posé par le propriétaire de la fenêtre (validation désactivée ici) n'est pas touché.
        glDisable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(nullptr, nullptr);
    }
    buffers_.forEach([](BufferHandle, BufferRes &r) { glDeleteBuffers(1, &r.id); });
    textures_.forEach([](TextureHandle, TextureRes &r) { glDeleteTextures(1, &r.id); });
    samplers_.forEach([](SamplerHandle, SamplerRes &r) { glDeleteSamplers(1, &r.id); });
    shaders_.forEach([](ShaderHandle, ShaderRes &r) { glDeleteShader(r.id); });
    pipelines_.forEach(
        [](PipelineHandle, PipelineRes &r)
        {
            glDeleteProgram(r.program);
            if (r.vao != 0)
            {
                glDeleteVertexArrays(1, &r.vao);
            }
        });
    queryPools_.forEach([](QueryPoolHandle, QueryPoolRes &r) { glDeleteQueries(static_cast<GLsizei>(r.ids.size()), r.ids.data()); });
    for (auto &[key, fbo] : framebuffers_)
    {
        glDeleteFramebuffers(1, &fbo);
    }
    glDeleteFramebuffers(1, &scratchFbo_);
    glDeleteVertexArrays(1, &emptyVao_);
    glDeleteBuffers(1, &pushBuffer_);
    if (ownsWindow_)
    {
        glfwDestroyWindow(window_);
    }
    else
    {
        glfwMakeContextCurrent(nullptr); // la fenêtre reste à son créateur
    }
}

DeviceStats GlDevice::stats() const noexcept
{
    DeviceStats s;
    s.buffers = static_cast<std::uint32_t>(buffers_.size());
    // Le backbuffer est une ressource interne : il ne compte pas parmi les textures de l'utilisateur.
    s.textures = static_cast<std::uint32_t>(textures_.size()) - 1u;
    s.samplers = static_cast<std::uint32_t>(samplers_.size());
    s.shaders = static_cast<std::uint32_t>(shaders_.size());
    s.pipelines = static_cast<std::uint32_t>(pipelines_.size());
    s.bindGroups = static_cast<std::uint32_t>(bindGroups_.size());
    s.queryPools = static_cast<std::uint32_t>(queryPools_.size());
    s.pendingDestruction = static_cast<std::uint32_t>(pending_.size());
    return s;
}

// ── Destruction différée ───────────────────────────────────────────────────────────────────────────────────────────

void GlDevice::queueRelease(std::function<void()> release) { pending_.push_back({frame_, std::move(release)}); }

void GlDevice::flushReleases(bool all)
{
    if (all)
    {
        // Appelé après glFinish (destructeur) : plus rien n'est en vol.
        for (Pending &p : pending_)
        {
            p.release();
        }
        pending_.clear();
        return;
    }
    if (frame_ < framesInFlight_)
    {
        return;
    }
    // Images dont les ressources peuvent partir : celles qui ont au moins framesInFlight_ images présentées après elles. Leur
    // clôture est attendue (normalement déjà franchie : c'est la limite d'avance du processeur sur le GPU).
    const std::uint64_t retired = frame_ - framesInFlight_;
    std::erase_if(fences_,
                  [&](const FrameFence &f)
                  {
                      if (f.frame > retired)
                      {
                          return false;
                      }
                      // Attente sans limite ; si elle échoue (clôture invalide), glFinish confirme la fin du travail avant de libérer.
                      if (glClientWaitSync(f.sync, GL_SYNC_FLUSH_COMMANDS_BIT, ~GLuint64{0}) == GL_WAIT_FAILED)
                      {
                          LOG_WARNING("OpenGL RHI: waiting on a frame fence failed, falling back to glFinish");
                          glFinish();
                      }
                      glDeleteSync(f.sync);
                      return true;
                  });
    std::erase_if(pending_,
                  [&](Pending &p)
                  {
                      if (p.frame <= retired)
                      {
                          p.release();
                          return true;
                      }
                      return false;
                  });
}

bool GlDevice::useCompleted(std::uint64_t serial)
{
    if (serial <= completedUse_)
    {
        return true;
    }
    if (serial >= useSerial_)
    {
        return false; // lot en cours : ses commandes sont déjà dans le flux GL, sans clôture
    }
    // Clôtures dans l'ordre des lots : la première non franchie arrête la recherche (les suivantes ne peuvent pas l'être).
    while (!useFences_.empty())
    {
        const UseFence &f = useFences_.front();
        const GLenum status = glClientWaitSync(f.sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
        {
            break; // GL_TIMEOUT_EXPIRED, ou GL_WAIT_FAILED : considéré comme non fini (le repli glBufferSubData reste juste)
        }
        completedUse_ = std::max(completedUse_, f.serial);
        glDeleteSync(f.sync);
        useFences_.pop_front();
        if (completedUse_ >= serial)
        {
            return true;
        }
    }
    // File vide : toutes les clôtures posées sont franchies. Un lot soumis sans clôture n'a utilisé aucun tampon mappé (ou glFinish
    // l'a déjà terminé, voir submit) : `serial` est fini.
    return useFences_.empty();
}

void GlDevice::pollUseFences()
{
    if (!useFences_.empty())
    {
        (void)useCompleted(useFences_.back().serial);
    }
}

void GlDevice::bindScratchTexture(GLenum target, GLuint id)
{
    glActiveTexture(GL_TEXTURE0 + kScratchTextureUnit);
    glBindTexture(target, id);
}

Result<GlDevice::VertexArrayBindings> GlDevice::inspectVertexArray(PipelineHandle handle)
{
    const PipelineRes *p = pipelines_.get(handle);
    if (p == nullptr || p->vao == 0)
    {
        return makeError(ErrorCode::kNotFound, "inspectVertexArray: stale, null or compute pipeline");
    }
    GLint current = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current);
    glBindVertexArray(p->vao);
    GLint vertex = 0;
    GLint index = 0;
    glGetIntegeri_v(GL_VERTEX_BINDING_BUFFER, 0, &vertex);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &index);
    glBindVertexArray(static_cast<GLuint>(current));
    return VertexArrayBindings{static_cast<GLuint>(vertex), static_cast<GLuint>(index)};
}

void GlDevice::labelQuery(const QueryPoolRes &pool, std::uint32_t index)
{
    if (!pool.debugName.empty())
    {
        label(GL_QUERY, pool.ids[index], pool.debugName + "[" + std::to_string(index) + "]");
    }
}

void GlDevice::eraseFramebuffersUsing(GLuint textureId)
{
    std::erase_if(framebuffers_,
                  [&](auto &entry)
                  {
                      if (std::ranges::find(entry.first.texture, textureId) != entry.first.texture.end())
                      {
                          glDeleteFramebuffers(1, &entry.second);
                          return true;
                      }
                      return false;
                  });
}

// ── Buffers ────────────────────────────────────────────────────────────────────────────────────────────────────────

Result<BufferHandle> GlDevice::createBuffer(const BufferDesc &desc, std::span<const std::byte> initial)
{
    if (desc.size == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "createBuffer: size is 0 (" + desc.debugName + ")");
    }
    if (desc.usage == BufferUsage::kNone)
    {
        return makeError(ErrorCode::kInvalidArgument, "createBuffer: no usage flag (" + desc.debugName + ")");
    }
    if (initial.size() > desc.size)
    {
        return makeError(ErrorCode::kInvalidArgument, "createBuffer: initial data larger than the buffer (" + desc.debugName + ")");
    }
    // Toute erreur de génération, de liaison ou de stockage est un échec : sinon glBufferData agirait sur un AUTRE tampon lié.
    GLuint id = 0;
    clearGlErrors();
    glGenBuffers(1, &id);
    glBindBuffer(GL_COPY_WRITE_BUFFER, id);
    // Un tampon kGpuOnly qui peut être réécrit (kCopyDst : updateBuffer, copies) est souvent mis à jour à chaque image (données
    // par image, sommets du batch 2D) : GL_DYNAMIC_DRAW. GL_STATIC_DRAW faisait émettre par le pilote un avertissement de
    // performance à chaque glBufferSubData.
    const bool rewritable = hasFlag(desc.usage, BufferUsage::kCopyDst);
    const GLenum hint = desc.memory == MemoryKind::kReadback ? GL_STREAM_READ
                        : (desc.memory == MemoryKind::kUpload || rewritable) ? GL_DYNAMIC_DRAW
                                                                              : GL_STATIC_DRAW;
    // kUpload : stockage immuable mappé une fois pour toutes (persistant, cohérent : pas de vidage explicite) ; GL_DYNAMIC_STORAGE_BIT
    // garde glBufferSubData pour une écriture pendant que le GPU lit encore le tampon (voir updateBuffer).
    const bool persistent = bufferStorage_ && desc.memory == MemoryKind::kUpload;
    constexpr GLbitfield kMapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    void *mapped = nullptr;
    if (id != 0 && !glFailed())
    {
        if (persistent)
        {
            glBufferStorage(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(desc.size), nullptr, kMapFlags | GL_DYNAMIC_STORAGE_BIT);
            if (!glFailed())
            {
                mapped = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(desc.size), kMapFlags);
            }
        }
        else
        {
            glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(desc.size), nullptr, hint);
        }
    }
    if (id == 0 || glFailed() || (persistent && mapped == nullptr))
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        glDeleteBuffers(1, &id);
        return makeError(ErrorCode::kOutOfRange, "createBuffer: the driver could not create or allocate " + std::to_string(desc.size) + " bytes (" +
                                                     desc.debugName + ")");
    }
    if (!initial.empty())
    {
        if (mapped != nullptr)
        {
            std::memcpy(mapped, initial.data(), initial.size()); // aucun GPU ne l'utilise encore
        }
        else
        {
            glBufferSubData(GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(initial.size()), initial.data());
        }
    }
    label(GL_BUFFER, id, desc.debugName);
    BufferRes res{id, desc};
    res.mapped = static_cast<std::byte *>(mapped);
    return buffers_.add(std::move(res));
}

Result<TextureDesc> GlDevice::textureDesc(TextureHandle handle) const
{
    const TextureRes *texture = textures_.get(handle);
    if (texture == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "textureDesc: stale or null texture handle");
    }
    return texture->desc;
}

Result<> GlDevice::updateBuffer(BufferHandle handle, std::uint64_t offset, std::span<const std::byte> data)
{
    BufferRes *buffer = buffers_.get(handle);
    if (buffer == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "updateBuffer: stale or null buffer handle");
    }
    if (offset > buffer->desc.size || data.size() > buffer->desc.size - offset)
    {
        return makeError(ErrorCode::kOutOfRange, "updateBuffer: range exceeds the buffer (" + buffer->desc.debugName + ")");
    }
    if (buffer->desc.memory == MemoryKind::kReadback)
    {
        return makeError(ErrorCode::kInvalidArgument, "updateBuffer: a Readback buffer is not writable by the CPU");
    }
    if (data.empty())
    {
        return {};
    }
    if (buffer->mapped != nullptr)
    {
        // Chemin rapide : plus aucun lot non terminé ne lit ce tampon (cas d'un anneau par image en vol) -> copie directe, visible
        // du GPU sans autre appel (cohérent). Sinon glBufferSubData, ordonné dans le flux GL comme avant : le résultat est le même,
        // seul le coût change.
        if (useCompleted(buffer->lastUse))
        {
            std::memcpy(buffer->mapped + offset, data.data(), data.size());
            ++mappedWrites_;
            return {};
        }
        buffer->lastUse = useSerial_; // l'écriture ordonnée est elle-même une opération du flux GL
        batchUsesMapped_ = true;
    }
    ++orderedWrites_;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer->id);
    glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size()), data.data());
    return {};
}

Result<std::vector<std::byte>> GlDevice::readBuffer(BufferHandle handle, std::uint64_t offset, std::uint64_t size)
{
    BufferRes *buffer = buffers_.get(handle);
    if (buffer == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "readBuffer: stale or null buffer handle");
    }
    if (offset > buffer->desc.size || size > buffer->desc.size - offset)
    {
        return makeError(ErrorCode::kOutOfRange, "readBuffer: range exceeds the buffer (" + buffer->desc.debugName + ")");
    }
    std::vector<std::byte> out(static_cast<std::size_t>(size));
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, buffer->id);
    glGetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), out.data());
    return out;
}

// ── Textures ───────────────────────────────────────────────────────────────────────────────────────────────────────

Result<TextureHandle> GlDevice::createTexture(const TextureDesc &desc)
{
    if (desc.debugName == "rhi.backbuffer")
    {
        return makeError(ErrorCode::kInvalidArgument, "createTexture: reserved debug name");
    }
    return createTextureImpl(desc);
}

Result<TextureHandle> GlDevice::createTextureImpl(const TextureDesc &requested)
{
    TextureDesc desc = requested;
    const GlFormat gf = glFormatOf(desc.format);
    if (gf.internal == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "createTexture: undefined format (" + desc.debugName + ")");
    }
    if (desc.usage == TextureUsage::kNone)
    {
        return makeError(ErrorCode::kInvalidArgument, "createTexture: no usage flag (" + desc.debugName + ")");
    }
    if (desc.size.width == 0 || desc.size.height == 0 || desc.size.depth == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "createTexture: empty extent (" + desc.debugName + ")");
    }
    if (formatInfo(desc.format).compressed())
    {
        const bool astc = desc.format == Format::kASTC4x4Unorm || desc.format == Format::kASTC4x4Srgb;
        if (!(astc ? caps_.astcTextures : caps_.bc7Textures))
        {
            return makeError(ErrorCode::kUnsupported, std::string("createTexture: compressed format ") + toString(desc.format) +
                                                          " is not supported by this device (" + desc.debugName + ")");
        }
        if (hasFlag(desc.usage, TextureUsage::kRenderTarget) || hasFlag(desc.usage, TextureUsage::kStorage) ||
            desc.dimension == TextureDimension::k3D)
        {
            return makeError(ErrorCode::kInvalidArgument, std::string("createTexture: compressed format ") + toString(desc.format) +
                                                              " is sampled only, 2D, array or cube (" + desc.debugName + ")");
        }
    }
    if (hasFlag(desc.usage, TextureUsage::kStorage) && !gf.imageCompatible)
    {
        return makeError(ErrorCode::kUnsupported,
                         std::string("createTexture: format ") + toString(desc.format) + " cannot be a storage image (" + desc.debugName + ")");
    }
    std::uint32_t layers = 1;
    switch (desc.dimension)
    {
        case TextureDimension::k2D: desc.size.depth = 1; break;
        case TextureDimension::k2DArray:
            layers = std::max(desc.layers, 1u);
            desc.size.depth = 1;
            break;
        case TextureDimension::kCube:
            layers = 6;
            desc.size.depth = 1;
            if (desc.size.width != desc.size.height)
            {
                return makeError(ErrorCode::kInvalidArgument, "createTexture: cube faces must be square (" + desc.debugName + ")");
            }
            break;
        case TextureDimension::k3D: break;
    }
    desc.layers = layers;
    const std::uint32_t maxDim = desc.dimension == TextureDimension::k3D ? caps_.max3DTextureSize : caps_.maxTextureSize;
    if (std::max({desc.size.width, desc.size.height, desc.size.depth}) > maxDim)
    {
        return makeError(ErrorCode::kOutOfRange, "createTexture: extent exceeds the device limit (" + desc.debugName + ")");
    }
    if (desc.dimension == TextureDimension::k2DArray)
    {
        GLint maxLayers = 0;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
        if (layers > static_cast<std::uint32_t>(maxLayers))
        {
            return makeError(ErrorCode::kOutOfRange, "createTexture: more array layers than the device allows (" + desc.debugName + ")");
        }
    }
    if (desc.dimension == TextureDimension::kCube)
    {
        GLint maxCube = 0;
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCube);
        if (desc.size.width > static_cast<std::uint32_t>(maxCube))
        {
            return makeError(ErrorCode::kOutOfRange, "createTexture: cube face exceeds the device limit (" + desc.debugName + ")");
        }
    }
    const std::uint32_t full = fullMipCount(desc.size.width, desc.size.height, desc.dimension == TextureDimension::k3D ? desc.size.depth : 1);
    const std::uint32_t levels = desc.mipLevels == 0 ? full : desc.mipLevels;
    if (levels > full)
    {
        return makeError(ErrorCode::kInvalidArgument, "createTexture: more mip levels than the extent allows (" + desc.debugName + ")");
    }
    desc.mipLevels = levels;

    TextureRes res;
    res.target = glTarget(desc.dimension);
    res.desc = desc;
    res.levels = levels;
    res.layers = layers;
    clearGlErrors();
    glGenTextures(1, &res.id);
    bindScratchTexture(res.target, res.id);
    const bool bound = res.id != 0 && !glFailed(); // sinon le stockage irait à la texture déjà liée sur l'unité de travail
    const auto w = static_cast<GLsizei>(desc.size.width);
    const auto h = static_cast<GLsizei>(desc.size.height);
    if (bound)
    {
        switch (desc.dimension)
        {
            case TextureDimension::k2D:
            case TextureDimension::kCube: glTexStorage2D(res.target, static_cast<GLsizei>(levels), gf.internal, w, h); break;
            case TextureDimension::k2DArray:
                glTexStorage3D(res.target, static_cast<GLsizei>(levels), gf.internal, w, h, static_cast<GLsizei>(layers));
                break;
            case TextureDimension::k3D:
                glTexStorage3D(res.target, static_cast<GLsizei>(levels), gf.internal, w, h, static_cast<GLsizei>(desc.size.depth));
                break;
        }
    }
    if (!bound || glFailed())
    {
        bindScratchTexture(res.target, 0);
        glDeleteTextures(1, &res.id);
        return makeError(ErrorCode::kOutOfRange, "createTexture: the driver could not create or allocate the texture (" + desc.debugName + ")");
    }
    label(GL_TEXTURE, res.id, desc.debugName);
    return textures_.add(std::move(res));
}

void GlDevice::releaseTexture(const TextureRes &texture)
{
    eraseFramebuffersUsing(texture.id);
    GLuint id = texture.id;
    glDeleteTextures(1, &id);
}

Result<SliceRegion> resolveRegion(const TextureRes &texture, const TextureCopy &copy)
{
    SliceRegion r;
    r.mip = copy.mipLevel;
    r.offset = copy.offset;
    if (copy.mipLevel >= texture.levels)
    {
        return makeError(ErrorCode::kOutOfRange, "texture region: mip level out of range (" + texture.desc.debugName + ")");
    }
    const std::uint32_t lw = mipDim(texture.desc.size.width, copy.mipLevel);
    const std::uint32_t lh = mipDim(texture.desc.size.height, copy.mipLevel);
    const bool is3D = texture.desc.dimension == TextureDimension::k3D;
    const bool layered = texture.desc.dimension == TextureDimension::k2DArray || texture.desc.dimension == TextureDimension::kCube;
    const std::uint32_t ld = is3D ? mipDim(texture.desc.size.depth, copy.mipLevel) : 1;
    if (layered)
    {
        r.layer = copy.layer;
        if (copy.layer >= texture.layers)
        {
            return makeError(ErrorCode::kOutOfRange, "texture region: layer out of range (" + texture.desc.debugName + ")");
        }
    }
    Extent3D size = copy.size;
    if (size.width == 0 && size.height == 0 && size.depth == 0)
    {
        size = {lw - static_cast<std::uint32_t>(std::max(copy.offset.x, 0)), lh - static_cast<std::uint32_t>(std::max(copy.offset.y, 0)),
                is3D ? ld - static_cast<std::uint32_t>(std::max(copy.offset.z, 0)) : (layered ? texture.layers - copy.layer : 1)};
    }
    if (size.width == 0 || size.height == 0 || size.depth == 0 || copy.offset.x < 0 || copy.offset.y < 0 || copy.offset.z < 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "texture region: empty or negative region (" + texture.desc.debugName + ")");
    }
    const bool fits = static_cast<std::uint64_t>(copy.offset.x) + size.width <= lw && static_cast<std::uint64_t>(copy.offset.y) + size.height <= lh &&
                      (is3D ? static_cast<std::uint64_t>(copy.offset.z) + size.depth <= ld
                            : (layered ? static_cast<std::uint64_t>(copy.layer) + size.depth <= texture.layers : size.depth == 1));
    if (!fits)
    {
        return makeError(ErrorCode::kOutOfRange, "texture region exceeds the level (" + texture.desc.debugName + ")");
    }
    r.size = size;
    return r;
}
Result<> GlDevice::updateTexture(TextureHandle handle, const TextureCopy &copy, std::span<const std::byte> data)
{
    TextureRes *texture = textures_.get(handle);
    if (texture == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "updateTexture: stale or null texture handle");
    }
    auto region = resolveRegion(*texture, copy);
    if (!region)
    {
        return std::unexpected(region.error());
    }
    if (texture->defaultFramebuffer)
    {
        return makeError(ErrorCode::kInvalidArgument, "updateTexture: the window backbuffer cannot be written by upload; render into it");
    }
    // Depuis la mémoire du processeur, les lignes sont serrées : `bufferRowLength` et `bufferOffset` ne concernent que les
    // copies tampon <-> texture (contrat de TextureCopy).
    const Format format = texture->desc.format;
    const std::uint64_t required = imageBytes(format, region->size.width, region->size.height, region->size.depth);
    if (data.size() < required)
    {
        return makeError(ErrorCode::kInvalidArgument, "updateTexture: not enough data for the region (" + texture->desc.debugName + ")");
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (formatInfo(format).compressed())
    {
        if (!blockAligned(format, region->offset.x, region->offset.y, region->size.width, region->size.height,
                          mipDim(texture->desc.size.width, region->mip), mipDim(texture->desc.size.height, region->mip)))
        {
            return makeError(ErrorCode::kInvalidArgument, "updateTexture: region is not aligned on 4x4 blocks (" + texture->desc.debugName + ")");
        }
        // Une couche par appel (uploadCompressedRegion) : sa taille doit tenir dans un GLsizei, sinon OpenGL refuserait l'envoi
        // en silence (un tableau BC7 8192 × 8192 × 32 fait 2 Gio, une couche 64 Mio).
        if (imageBytes(format, region->size.width, region->size.height) > static_cast<std::uint64_t>(std::numeric_limits<GLsizei>::max()))
        {
            return makeError(ErrorCode::kOutOfRange, "updateTexture: one layer of the region exceeds the GL transfer size limit (" +
                                                         texture->desc.debugName + ")");
        }
        uploadCompressedRegion(*texture, *region, data.first(required));
        return {};
    }
    uploadRegion(*texture, *region, 0, reinterpret_cast<std::uintptr_t>(data.data()));
    return {};
}

void GlDevice::uploadRegion(const TextureRes &texture, const SliceRegion &region, std::uint32_t rowLength, std::uintptr_t source)
{
    const GlFormat gf = glFormatOf(texture.desc.format);
    const std::uint32_t bpt = formatInfo(texture.desc.format).bytesPerTexel;
    const std::uint64_t rowTexels = rowLength != 0 ? rowLength : region.size.width;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(rowLength));
    bindScratchTexture(texture.target, texture.id); // n'écrase pas une texture liée par setBindGroup
    const auto mip = static_cast<GLint>(region.mip);
    const auto x = region.offset.x, y = region.offset.y;
    const auto w = static_cast<GLsizei>(region.size.width), h = static_cast<GLsizei>(region.size.height);
    const auto d = static_cast<GLsizei>(region.size.depth);
    const auto at = [&](std::uint64_t bytes) { return reinterpret_cast<const void *>(source + bytes); };
    switch (texture.desc.dimension)
    {
        case TextureDimension::k2D: glTexSubImage2D(GL_TEXTURE_2D, mip, x, y, w, h, gf.format, gf.type, at(0)); break;
        case TextureDimension::k2DArray:
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, mip, x, y, static_cast<GLint>(region.layer), w, h, d, gf.format, gf.type, at(0));
            break;
        case TextureDimension::kCube:
            for (GLsizei face = 0; face < d; ++face)
            {
                glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.layer + static_cast<GLuint>(face), mip, x, y, w, h, gf.format, gf.type,
                                at(rowTexels * region.size.height * bpt * static_cast<std::uint64_t>(face)));
            }
            break;
        case TextureDimension::k3D:
            glTexSubImage3D(GL_TEXTURE_3D, mip, x, y, region.offset.z, w, h, d, gf.format, gf.type, at(0));
            break;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void GlDevice::uploadCompressedRegion(const TextureRes &texture, const SliceRegion &region, std::span<const std::byte> data)
{
    const GLenum internal = glFormatOf(texture.desc.format).internal;
    bindScratchTexture(texture.target, texture.id); // n'écrase pas une texture liée par setBindGroup
    const auto mip = static_cast<GLint>(region.mip);
    const auto x = region.offset.x, y = region.offset.y;
    const auto w = static_cast<GLsizei>(region.size.width), h = static_cast<GLsizei>(region.size.height);
    const std::uint64_t slice = imageBytes(texture.desc.format, region.size.width, region.size.height);
    switch (texture.desc.dimension)
    {
        case TextureDimension::k2D:
            glCompressedTexSubImage2D(GL_TEXTURE_2D, mip, x, y, w, h, internal, static_cast<GLsizei>(slice), data.data());
            break;
        case TextureDimension::k2DArray:
            for (std::uint32_t layer = 0; layer < region.size.depth; ++layer) // couche par couche : `slice` tient dans un GLsizei
            {
                glCompressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, mip, x, y, static_cast<GLint>(region.layer + layer), w, h, 1, internal,
                                          static_cast<GLsizei>(slice), data.data() + slice * layer);
            }
            break;
        case TextureDimension::kCube:
            for (std::uint32_t face = 0; face < region.size.depth; ++face)
            {
                glCompressedTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.layer + face, mip, x, y, w, h, internal,
                                          static_cast<GLsizei>(slice), data.data() + slice * face);
            }
            break;
        case TextureDimension::k3D: break; // refusé à la création
    }
}

Result<> GlDevice::readRegion(const TextureRes &texture, const SliceRegion &region, std::uint32_t rowLength, std::uintptr_t destination)
{
    const GlFormat gf = glFormatOf(texture.desc.format);
    const FormatInfo info = formatInfo(texture.desc.format);
    const std::uint64_t rowTexels = rowLength != 0 ? rowLength : region.size.width;
    const std::uint64_t sliceBytes = rowTexels * region.size.height * info.bytesPerTexel;
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    // Une lecture synchrone reste permise pendant une passe : on rend la liaison qu'on a trouvée, que l'encodeur suppose intacte.
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    if (texture.defaultFramebuffer)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        // La fenêtre range sa ligne 0 en BAS de l'écran ; le RHI rend la ligne 0 = haut : lecture ligne par ligne, retournée.
        const std::uint64_t rowBytes = rowTexels * info.bytesPerTexel;
        const auto height = static_cast<GLint>(texture.desc.size.height);
        for (std::uint32_t r = 0; r < region.size.height; ++r)
        {
            const GLint glRow = height - 1 - (region.offset.y + static_cast<GLint>(r));
            glReadPixels(region.offset.x, glRow, static_cast<GLsizei>(region.size.width), 1, gf.format, gf.type,
                         reinterpret_cast<void *>(destination + rowBytes * r));
        }
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        return {};
    }
    glBindFramebuffer(GL_FRAMEBUFFER, scratchFbo_);
    glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(rowLength));
    const GLenum attachment = info.kind == FormatKind::kDepth          ? GL_DEPTH_ATTACHMENT
                              : info.kind == FormatKind::kDepthStencil ? GL_DEPTH_STENCIL_ATTACHMENT
                                                                       : GL_COLOR_ATTACHMENT0;
    Result<> result;
    for (std::uint32_t slice = 0; slice < region.size.depth && result; ++slice)
    {
        switch (texture.desc.dimension)
        {
            case TextureDimension::k2D:
                glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture.id, static_cast<GLint>(region.mip));
                break;
            case TextureDimension::kCube:
                glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.layer + slice, texture.id,
                                       static_cast<GLint>(region.mip));
                break;
            case TextureDimension::k2DArray:
                glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture.id, static_cast<GLint>(region.mip),
                                          static_cast<GLint>(region.layer + slice));
                break;
            case TextureDimension::k3D:
                glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture.id, static_cast<GLint>(region.mip),
                                          region.offset.z + static_cast<GLint>(slice));
                break;
        }
        glReadBuffer(attachment == GL_COLOR_ATTACHMENT0 ? GL_COLOR_ATTACHMENT0 : GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            result = makeError(ErrorCode::kUnsupported, std::string("read back: format ") + toString(texture.desc.format) +
                                                            " cannot be read (" + texture.desc.debugName + ")");
            break;
        }
        glReadPixels(region.offset.x, region.offset.y, static_cast<GLsizei>(region.size.width), static_cast<GLsizei>(region.size.height), gf.format,
                     gf.type, reinterpret_cast<void *>(destination + sliceBytes * slice));
    }
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    return result;
}

Result<std::vector<std::byte>> GlDevice::readTexture(TextureHandle handle, const TextureCopy &copy)
{
    TextureRes *texture = textures_.get(handle);
    if (texture == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "readTexture: stale or null texture handle");
    }
    if (formatInfo(texture->desc.format).compressed())
    {
        return makeError(ErrorCode::kUnsupported, "readTexture: compressed textures cannot be read back (" + texture->desc.debugName + ")");
    }
    auto region = resolveRegion(*texture, copy);
    if (!region)
    {
        return std::unexpected(region.error());
    }
    const FormatInfo info = formatInfo(texture->desc.format);
    std::vector<std::byte> out(static_cast<std::size_t>(region->size.width) * region->size.height * region->size.depth * info.bytesPerTexel);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (auto r = readRegion(*texture, *region, 0, reinterpret_cast<std::uintptr_t>(out.data())); !r)
    {
        return std::unexpected(r.error());
    }
    return out;
}

// ── Échantillonneurs, shaders ──────────────────────────────────────────────────────────────────────────────────────

Result<SamplerHandle> GlDevice::createSampler(const SamplerDesc &desc)
{
    if (desc.maxAnisotropy > 1.0f && !caps_.anisotropy)
    {
        return makeError(ErrorCode::kUnsupported, "createSampler: anisotropic filtering is not available on this device");
    }
    GLuint id = 0;
    glGenSamplers(1, &id);
    glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(glMinFilter(desc.minFilter, desc.mipFilter)));
    glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(glFilter(desc.magFilter)));
    glSamplerParameteri(id, GL_TEXTURE_WRAP_S, static_cast<GLint>(glAddress(desc.addressU)));
    glSamplerParameteri(id, GL_TEXTURE_WRAP_T, static_cast<GLint>(glAddress(desc.addressV)));
    glSamplerParameteri(id, GL_TEXTURE_WRAP_R, static_cast<GLint>(glAddress(desc.addressW)));
    glSamplerParameterfv(id, GL_TEXTURE_BORDER_COLOR, desc.borderColor.data());
    glSamplerParameterf(id, GL_TEXTURE_MIN_LOD, desc.minLod);
    glSamplerParameterf(id, GL_TEXTURE_MAX_LOD, desc.maxLod);
    if (desc.compare)
    {
        glSamplerParameteri(id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(id, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(glCompare(*desc.compare)));
    }
    if (desc.maxAnisotropy > 1.0f)
    {
        constexpr GLenum kTextureMaxAnisotropy = 0x84FE; // GL_TEXTURE_MAX_ANISOTROPY (EXT et ARB : même valeur)
        glSamplerParameterf(id, kTextureMaxAnisotropy, desc.maxAnisotropy);
    }
    label(GL_SAMPLER, id, desc.debugName);
    return samplers_.add({id});
}

Result<ShaderHandle> GlDevice::createShader(const ShaderDesc &desc)
{
    if (desc.language == ShaderLanguage::kSpirv)
    {
        return makeError(ErrorCode::kUnsupported, "createShader: SPIR-V is not accepted by the OpenGL backend (Capabilities::spirvShaders is false)");
    }
    if (desc.code.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "createShader: empty source (" + desc.debugName + ")");
    }
    const GLuint id = glCreateShader(glShaderStage(desc.stage));
    const auto *source = reinterpret_cast<const GLchar *>(desc.code.data());
    const auto length = static_cast<GLint>(desc.code.size());
    glShaderSource(id, 1, &source, &length);
    glCompileShader(id);
    GLint ok = GL_FALSE;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        std::string log = shaderLog(id);
        glDeleteShader(id);
        return makeError(ErrorCode::kParse, "createShader (" + desc.debugName + "): " + log);
    }
    label(GL_SHADER, id, desc.debugName);
    return shaders_.add({id, desc.stage});
}

namespace
{
Result<GLuint> linkProgram(std::span<const GLuint> shaders, const std::string &name)
{
    const GLuint program = glCreateProgram();
    for (GLuint s : shaders)
    {
        glAttachShader(program, s);
    }
    glLinkProgram(program);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    for (GLuint s : shaders)
    {
        glDetachShader(program, s);
    }
    if (ok != GL_TRUE)
    {
        std::string log = programLog(program);
        glDeleteProgram(program);
        return makeError(ErrorCode::kParse, "link (" + name + "): " + log);
    }
    label(GL_PROGRAM, program, name);
    return program;
}
} // namespace

namespace
{
// Le programme lié accède-t-il au stockage ? Blocs de stockage actifs, ou uniforme actif d'un type image (GL_IMAGE_1D à
// GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE_ARRAY, plage contiguë de valeurs).
bool programUsesStorage(GLuint program)
{
    GLint blocks = 0;
    glGetProgramInterfaceiv(program, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &blocks);
    if (blocks > 0)
    {
        return true;
    }
    GLint uniforms = 0;
    glGetProgramInterfaceiv(program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &uniforms);
    for (GLint i = 0; i < uniforms; ++i)
    {
        const GLenum property = GL_TYPE;
        GLint type = 0;
        glGetProgramResourceiv(program, GL_UNIFORM, static_cast<GLuint>(i), 1, &property, 1, nullptr, &type);
        if (type >= GL_IMAGE_1D && type <= GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE_ARRAY)
        {
            return true;
        }
    }
    return false;
}
} // namespace

Result<PipelineHandle> GlDevice::createGraphicsPipeline(const GraphicsPipelineDesc &desc)
{
    const ShaderRes *vs = shaders_.get(desc.vertex);
    const ShaderRes *fs = shaders_.get(desc.fragment);
    if (vs == nullptr || vs->stage != ShaderStage::kVertex || fs == nullptr || fs->stage != ShaderStage::kFragment)
    {
        return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: needs a live vertex and a live fragment shader (" + desc.debugName + ")");
    }
    if (desc.colorTargets.size() > caps_.maxColorAttachments)
    {
        return makeError(ErrorCode::kOutOfRange, "createGraphicsPipeline: too many color targets (" + desc.debugName + ")");
    }
    for (const ColorTargetState &t : desc.colorTargets)
    {
        if (formatInfo(t.format).bytesPerTexel == 0 || formatInfo(t.format).isDepth())
        {
            return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: invalid color target format (" + desc.debugName + ")");
        }
    }
    if (desc.depthFormat != Format::kUndefined && !formatInfo(desc.depthFormat).isDepth())
    {
        return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: depthFormat is not a depth format (" + desc.debugName + ")");
    }
    PipelineRes res;
    for (const VertexBufferLayout &vb : desc.vertexBuffers)
    {
        if (vb.binding >= res.strides.size())
        {
            return makeError(ErrorCode::kOutOfRange, "createGraphicsPipeline: vertex buffer binding >= 16 (" + desc.debugName + ")");
        }
        res.strides[vb.binding] = vb.stride;
        res.bindingUsed[vb.binding] = true;
    }
    for (const VertexAttribute &a : desc.vertexAttributes)
    {
        if (a.binding >= res.bindingUsed.size() || !res.bindingUsed[a.binding])
        {
            return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: attribute uses an undeclared vertex buffer binding (" + desc.debugName + ")");
        }
    }

    const GLuint shaderIds[] = {vs->id, fs->id};
    auto program = linkProgram(shaderIds, desc.debugName);
    if (!program)
    {
        return std::unexpected(program.error());
    }
    res.program = *program;
    res.usesStorage = programUsesStorage(res.program);
    glGenVertexArrays(1, &res.vao);
    glBindVertexArray(res.vao);
    for (const VertexAttribute &a : desc.vertexAttributes)
    {
        const GlVertexFormat vf = glVertexFormat(a.format);
        glEnableVertexAttribArray(a.location);
        if (vf.integer)
        {
            glVertexAttribIFormat(a.location, vf.components, vf.type, a.offset);
        }
        else
        {
            glVertexAttribFormat(a.location, vf.components, vf.type, vf.normalized ? GL_TRUE : GL_FALSE, a.offset);
        }
        glVertexAttribBinding(a.location, a.binding);
    }
    for (const VertexBufferLayout &vb : desc.vertexBuffers)
    {
        glVertexBindingDivisor(vb.binding, vb.stepRate == VertexStepRate::kInstance ? 1 : 0);
    }
    glBindVertexArray(emptyVao_);
    label(GL_VERTEX_ARRAY, res.vao, desc.debugName);

    res.compute = false;
    res.topology = glTopology(desc.topology);
    res.raster = desc.raster;
    res.depth = desc.depth;
    res.colorTargets = desc.colorTargets;
    res.depthFormat = desc.depthFormat;
    return pipelines_.add(std::move(res));
}

Result<PipelineHandle> GlDevice::createComputePipeline(const ComputePipelineDesc &desc)
{
    const ShaderRes *cs = shaders_.get(desc.compute);
    if (cs == nullptr || cs->stage != ShaderStage::kCompute)
    {
        return makeError(ErrorCode::kInvalidArgument, "createComputePipeline: needs a live compute shader (" + desc.debugName + ")");
    }
    const GLuint shaderIds[] = {cs->id};
    auto program = linkProgram(shaderIds, desc.debugName);
    if (!program)
    {
        return std::unexpected(program.error());
    }
    PipelineRes res;
    res.compute = true;
    res.program = *program;
    return pipelines_.add(std::move(res));
}

// ── Groupes de liaison, pools de requêtes ──────────────────────────────────────────────────────────────────────────

Result<BindGroupHandle> GlDevice::createBindGroup(const BindGroupDesc &desc)
{
    std::uint32_t seen = 0;
    for (const BindGroupEntry &e : desc.entries)
    {
        const std::string where = " (bind group " + desc.debugName + ", binding " + std::to_string(e.binding) + ")";
        if (e.binding >= kBindingsPerGroup)
        {
            return makeError(ErrorCode::kOutOfRange, "createBindGroup: binding >= kBindingsPerGroup" + where);
        }
        if ((seen & (1u << e.binding)) != 0)
        {
            return makeError(ErrorCode::kAlreadyExists, "createBindGroup: duplicate binding" + where);
        }
        seen |= 1u << e.binding;
        switch (e.type)
        {
            case BindingType::kUniformBuffer:
            case BindingType::kStorageBuffer:
            {
                const BufferRes *b = buffers_.get(e.buffer);
                if (b == nullptr)
                {
                    return makeError(ErrorCode::kNotFound, "createBindGroup: stale or null buffer" + where);
                }
                const BufferUsage need = e.type == BindingType::kUniformBuffer ? BufferUsage::kUniform : BufferUsage::kStorage;
                if (!hasFlag(b->desc.usage, need))
                {
                    return makeError(ErrorCode::kInvalidArgument, "createBindGroup: buffer lacks the Uniform/Storage usage" + where);
                }
                const std::uint64_t align = e.type == BindingType::kUniformBuffer ? caps_.minUniformAlignment : caps_.minStorageAlignment;
                if (e.offset % align != 0 || e.offset > b->desc.size || e.size > b->desc.size - e.offset)
                {
                    return makeError(ErrorCode::kOutOfRange, "createBindGroup: offset misaligned or range exceeds the buffer" + where);
                }
                break;
            }
            case BindingType::kSampledTexture:
            {
                const TextureRes *t = textures_.get(e.texture);
                if (t == nullptr || samplers_.get(e.sampler) == nullptr)
                {
                    return makeError(ErrorCode::kNotFound, "createBindGroup: stale or null texture/sampler" + where);
                }
                if (!hasFlag(t->desc.usage, TextureUsage::kSampled))
                {
                    return makeError(ErrorCode::kInvalidArgument, "createBindGroup: texture lacks the Sampled usage" + where);
                }
                break;
            }
            case BindingType::kStorageImage:
            {
                const TextureRes *t = textures_.get(e.texture);
                if (t == nullptr)
                {
                    return makeError(ErrorCode::kNotFound, "createBindGroup: stale or null texture" + where);
                }
                if (!hasFlag(t->desc.usage, TextureUsage::kStorage))
                {
                    return makeError(ErrorCode::kInvalidArgument, "createBindGroup: texture lacks the Storage usage" + where);
                }
                break;
            }
        }
    }
    return bindGroups_.add({desc.entries});
}

Result<QueryPoolHandle> GlDevice::createQueryPool(std::uint32_t count, const char *debugName)
{
    if (count == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "createQueryPool: count is 0");
    }
    QueryPoolRes res;
    res.ids.resize(count);
    res.written.assign(count, false);
    glGenQueries(static_cast<GLsizei>(count), res.ids.data());
    // Un objet de requête n'existe qu'après son premier usage : le nom est posé par writeTimestamp (labelQuery).
    res.debugName = debugName != nullptr ? debugName : "";
    return queryPools_.add(std::move(res));
}

Result<bool> GlDevice::timestampsReady(QueryPoolHandle handle, std::uint32_t first, std::uint32_t count)
{
    QueryPoolRes *pool = queryPools_.get(handle);
    if (pool == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "timestampsReady: stale or null query pool");
    }
    if (first > pool->ids.size() || count > pool->ids.size() - first)
    {
        return makeError(ErrorCode::kOutOfRange, "timestampsReady: range exceeds the pool");
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!pool->written[first + i])
        {
            return false;
        }
        GLint available = GL_FALSE;
        glGetQueryObjectiv(pool->ids[first + i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available != GL_TRUE)
        {
            return false;
        }
    }
    return true;
}

Result<std::vector<std::uint64_t>> GlDevice::readTimestamps(QueryPoolHandle handle, std::uint32_t first, std::uint32_t count)
{
    QueryPoolRes *pool = queryPools_.get(handle);
    if (pool == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "readTimestamps: stale or null query pool");
    }
    if (first > pool->ids.size() || count > pool->ids.size() - first)
    {
        return makeError(ErrorCode::kOutOfRange, "readTimestamps: range exceeds the pool");
    }
    std::vector<std::uint64_t> out(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!pool->written[first + i])
        {
            return makeError(ErrorCode::kInvalidArgument, "readTimestamps: timestamp " + std::to_string(first + i) + " was never written");
        }
        GLuint64 value = 0;
        glGetQueryObjectui64v(pool->ids[first + i], GL_QUERY_RESULT, &value);
        out[i] = value;
    }
    return out;
}

// ── Destruction ────────────────────────────────────────────────────────────────────────────────────────────────────

void GlDevice::destroy(BufferHandle handle)
{
    if (auto res = buffers_.remove(handle))
    {
        queueRelease([id = res->id]() mutable { glDeleteBuffers(1, &id); });
    }
}

void GlDevice::destroy(TextureHandle handle)
{
    if (handle == backbuffer_)
    {
        return; // appartient au périphérique
    }
    if (auto res = textures_.remove(handle))
    {
        queueRelease([this, tex = *res]() { releaseTexture(tex); });
    }
}

void GlDevice::destroy(SamplerHandle handle)
{
    if (auto res = samplers_.remove(handle))
    {
        queueRelease([id = res->id]() mutable { glDeleteSamplers(1, &id); });
    }
}

void GlDevice::destroy(ShaderHandle handle)
{
    if (auto res = shaders_.remove(handle))
    {
        queueRelease([id = res->id]() { glDeleteShader(id); });
    }
}

void GlDevice::destroy(PipelineHandle handle)
{
    if (auto res = pipelines_.remove(handle))
    {
        queueRelease(
            [program = res->program, vao = res->vao]() mutable
            {
                glDeleteProgram(program);
                if (vao != 0)
                {
                    glDeleteVertexArrays(1, &vao);
                }
            });
    }
}

void GlDevice::destroy(BindGroupHandle handle)
{
    if (bindGroups_.remove(handle))
    {
        queueRelease([] {}); // aucune ressource GL ; passe par la file pour que `pendingDestruction` soit homogène
    }
}

void GlDevice::destroy(QueryPoolHandle handle)
{
    if (auto res = queryPools_.remove(handle))
    {
        queueRelease([ids = std::move(res->ids)]() { glDeleteQueries(static_cast<GLsizei>(ids.size()), ids.data()); });
    }
}

// ── Images, redimensionnement ──────────────────────────────────────────────────────────────────────────────────────

Result<> GlDevice::refreshWindowBackbuffer()
{
    int w = 0, h = 0;
    platform::framebufferSizeOf(window_, w, h); // gardée par la Window : pas d'aller-retour avec le serveur d'affichage
    if (w <= 0 || h <= 0)
    {
        return makeError(ErrorCode::kBusy, "the window has an empty framebuffer (minimized): skip the frame");
    }
    TextureRes *bb = textures_.get(backbuffer_);
    bb->desc.size = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
    backbufferSize_ = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
    return {};
}

Result<TextureHandle> GlDevice::beginFrame()
{
    if (windowed_)
    {
        if (auto r = refreshWindowBackbuffer(); !r)
        {
            return std::unexpected(r.error());
        }
    }
    return backbuffer_;
}

Result<> GlDevice::present()
{
    if (windowed_)
    {
        glfwSwapBuffers(window_);
    }
    if (GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); fence != nullptr)
    {
        fences_.push_back({frame_, fence});
    }
    else
    {
        // Pas de clôture : glFinish garantit tout de suite que l'image est terminée (les libérations restent sûres).
        LOG_WARNING("OpenGL RHI: glFenceSync failed, falling back to glFinish for this frame");
        glFinish();
    }
    ++frame_;
    flushReleases(false);
    pollUseFences(); // libère les clôtures de lots franchies, même si aucune écriture mappée ne les interroge
    return {};
}

void GlDevice::setVSync(bool enabled)
{
    if (windowed_)
    {
        glfwSwapInterval(enabled ? 1 : 0);
    }
}

Result<> GlDevice::resize(Extent2D size)
{
    if (windowed_)
    {
        return refreshWindowBackbuffer();
    }
    if (size.width == 0 || size.height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "resize: empty size");
    }
    TextureRes *bb = textures_.get(backbuffer_);
    const TextureRes old = *bb;

    TextureRes fresh = old;
    fresh.desc.size = {size.width, size.height, 1};
    fresh.levels = 1;
    if (size.width > caps_.maxTextureSize || size.height > caps_.maxTextureSize)
    {
        return makeError(ErrorCode::kOutOfRange, "resize: size exceeds the device limit"); // le backbuffer actuel reste intact
    }
    clearGlErrors();
    glGenTextures(1, &fresh.id);
    bindScratchTexture(fresh.target, fresh.id);
    const bool bound = fresh.id != 0 && !glFailed();
    if (bound)
    {
        glTexStorage2D(fresh.target, 1, GL_RGBA8, static_cast<GLsizei>(size.width), static_cast<GLsizei>(size.height));
    }
    if (!bound || glFailed())
    {
        bindScratchTexture(fresh.target, 0);
        glDeleteTextures(1, &fresh.id);
        return makeError(ErrorCode::kOutOfRange, "resize: the driver could not allocate the new backbuffer"); // l'ancien reste intact
    }
    label(GL_TEXTURE, fresh.id, fresh.desc.debugName);
    queueRelease([this, old]() { releaseTexture(old); }); // seulement après le succès
    *bb = fresh; // le handle reste valide
    backbufferSize_ = size;
    return {};
}

void GlDevice::waitIdle() { glFinish(); }

GLuint GlDevice::framebufferFor(const FboKey &key, std::uint32_t colorCount,
                                const std::array<const TextureRes *, kMaxAttachments + 1> &attachments)
{
    for (auto &[k, fbo] : framebuffers_)
    {
        if (k == key)
        {
            return fbo;
        }
    }
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    auto attach = [&](GLenum point, const TextureRes &t, std::uint32_t mip, std::uint32_t layer)
    {
        switch (t.desc.dimension)
        {
            case TextureDimension::k2D: glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_2D, t.id, static_cast<GLint>(mip)); break;
            case TextureDimension::kCube:
                glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, t.id, static_cast<GLint>(mip));
                break;
            case TextureDimension::k2DArray:
            case TextureDimension::k3D:
                glFramebufferTextureLayer(GL_FRAMEBUFFER, point, t.id, static_cast<GLint>(mip), static_cast<GLint>(layer));
                break;
        }
    };
    for (std::uint32_t i = 0; i < colorCount; ++i)
    {
        attach(GL_COLOR_ATTACHMENT0 + i, *attachments[i], key.mip[i], key.layer[i]);
    }
    if (const TextureRes *depth = attachments[kMaxAttachments]; depth != nullptr)
    {
        const bool stencil = depth->desc.format == Format::kD24UnormS8Uint;
        attach(stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, *depth, key.mip[kMaxAttachments], key.layer[kMaxAttachments]);
    }
    framebuffers_.emplace_back(key, fbo);
    return fbo;
}

} // namespace orosmo::rhi::gl
