#include "FreeAge/client/texture.hpp"

#include <mango/image/image.hpp>

#include "FreeAge/client/opengl.hpp"


// TODO: Implement in a nicer way.
// TODO: Does not account for mip-maps or possible additional bytes used for alignment by the driver.
usize debugUsedGPUMemory = 0;

static void PrintGPUMemoryUsage() {
  LOG(1) << "Approx. GPU memory usage: " << static_cast<int>(debugUsedGPUMemory / (1024.f * 1024.f) + 0.5f) << " MB";
}


Texture* TextureManager::GetOrLoad(const std::filesystem::path& path, Loader loader, int wrapMode, int magFilter, int minFilter) {
  TextureSettings settings(path.string(), wrapMode, magFilter, minFilter);
  auto it = loadedTextures.find(settings);
  if (it != loadedTextures.end()) {
    it->second->AddReference();
    return it->second;
  }
  
  // Load the texture.
  Texture* newTexture = new Texture();
  if (loader == Loader::QImage) {
    QImage image(path.string().c_str());
    if (image.isNull()) {
      LOG(ERROR) << "Failed to load as QImage: " << path;
      return nullptr;
    }
    newTexture->Load(image, wrapMode, magFilter, minFilter);
  } else if (loader == Loader::Mango) {
    if (!newTexture->Load(path, wrapMode, magFilter, minFilter)) {
      LOG(ERROR) << "Failed to load with Mango: " << path;
      return nullptr;
    }
  } else {
    LOG(FATAL) << "Invalid loader specified: " << static_cast<int>(loader);
  }
  
  loadedTextures.insert(std::make_pair(settings, newTexture));
  newTexture->AddReference();
  return newTexture;
}

void TextureManager::Dereference(Texture* texture) {
  if (!texture->RemoveReference()) {
    return;
  }
  
  for (auto it = loadedTextures.begin(), end = loadedTextures.end(); it != end; ++ it) {
    if (it->second == texture) {
      loadedTextures.erase(it);
      delete texture;
      return;
    }
  }
  
  LOG(ERROR) << "The reference count for a texture reached zero, but it could not be found in loadedTextures to remove it from there.";
  delete texture;
}

TextureManager::~TextureManager() {
  for (const auto& item : loadedTextures) {
    LOG(ERROR) << "Texture still loaded on TextureManager destruction: " << item.first.path << " (references: " << item.second->GetReferenceCount() << ")";
  }
}


Texture::~Texture() {
  if (width != -1) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    f->glDeleteTextures(1, &textureId);
    
    debugUsedGPUMemory -= width * height * bytesPerPixel;
    // NOTE: We do not print the new memory usage here to prevent log spam on program exit.
    // PrintGPUMemoryUsage();
  }
}

void Texture::CreateEmpty(int width, int height, int wrapMode, int magFilter, int minFilter) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  this->width = width;
  this->height = height;
  bytesPerPixel = 4;
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
  
  f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
  
  debugUsedGPUMemory += width * height * bytesPerPixel;
  PrintGPUMemoryUsage();
  CHECK_OPENGL_NO_ERROR();
}

void Texture::Load(const QImage& image, int wrapMode, int magFilter, int minFilter) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  width = image.width();
  height = image.height();
  bytesPerPixel = (image.format() == QImage::Format_ARGB32) ? 4 : 1;
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
  
  // QImage scan lines are aligned to multiples of 4 bytes. Ensure that OpenGL reads this correctly.
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  
  if (image.format() == QImage::Format_ARGB32) {
    f->glTexImage2D(
        GL_TEXTURE_2D,
        0, GL_RGBA,
        image.width(), image.height(),
        0, GL_BGRA, GL_UNSIGNED_BYTE,
        image.scanLine(0));
  } else if (image.format() == QImage::Format_Grayscale8) {
    f->glTexImage2D(
        GL_TEXTURE_2D,
        0, GL_RED,
        image.width(), image.height(),
        0, GL_RED, GL_UNSIGNED_BYTE,
        image.scanLine(0));
  } else {
    LOG(FATAL) << "Unsupported QImage format.";
  }
  
  debugUsedGPUMemory += width * height * bytesPerPixel;
  PrintGPUMemoryUsage();
  CHECK_OPENGL_NO_ERROR();
}

bool Texture::Load(const std::filesystem::path& path, int wrapMode, int magFilter, int minFilter) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  mango::Bitmap bitmap(path.string(), mango::Format(32, mango::Format::UNORM, mango::Format::BGRA, 8, 8, 8, 8));
  if (bitmap.width <= 0) {
    return false;
  }
  
  width = bitmap.width;
  height = bitmap.height;
  bytesPerPixel = 4;
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
  
  // QImage scan lines are aligned to multiples of 4 bytes. Ensure that OpenGL reads this correctly.
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      bitmap.width, bitmap.height,
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      bitmap.address<u32>(0, 0));
  
  debugUsedGPUMemory += width * height * bytesPerPixel;
  PrintGPUMemoryUsage();
  CHECK_OPENGL_NO_ERROR();
  return true;
}
