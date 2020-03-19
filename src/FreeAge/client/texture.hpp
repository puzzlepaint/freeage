#pragma once

#include <filesystem>
#include <unordered_map>

#include <QImage>
#include <QOpenGLFunctions_3_2_Core>

class Texture;


/// Singleton class which keeps track of loaded textures (with reference counting)
/// in order to avoid duplicate loading of textures.
class TextureManager {
 public:
  enum class Loader {
    QImage = 0,
    Mango
  };
  
  static TextureManager& Instance() {
    static TextureManager instance;
    return instance;
  }
  
  /// Loads the given texture with the given settings, or returns an existing instance if available.
  /// The returned pointer must not be freed, but must be passed to Dereference() once it is not needed anymore.
  /// The function returns nullptr if it fails to load the given file.
  Texture* GetOrLoad(const std::filesystem::path& path, Loader loader, int wrapMode, int magFilter, int minFilter);
  
  /// Must be called once the texture is not needed anymore. Once all references are gone, the texture is unloaded.
  void Dereference(Texture* texture);
  
 private:
  struct TextureSettings {
    inline TextureSettings(std::string path, int wrapMode, int magFilter, int minFilter)
        : path(path), wrapMode(wrapMode), magFilter(magFilter), minFilter(minFilter) {}
    
    inline bool operator== (const TextureSettings& other) const noexcept {
      return path == other.path &&
             wrapMode == other.wrapMode &&
             magFilter == other.magFilter &&
             minFilter == other.minFilter;
    }
    
    std::string path;
    int wrapMode;
    int magFilter;
    int minFilter;
  };
  
  struct TextureHash {
    // Variadic hash combination function from:
    // https://stackoverflow.com/questions/2590677
    template <typename T, typename... Rest>
    inline void hashCombine(std::size_t& seed, const T& v, Rest... rest) const {
      std::hash<T> hasher;
      seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      (hashCombine(seed, rest), ...);
    }
    
    inline std::size_t operator()(const TextureSettings& s) const noexcept {
      std::size_t h = 0;
      hashCombine(h, s.path, s.wrapMode, s.magFilter, s.minFilter);
      return (size_t) std::hash<std::string>()(s.path);
    }
  };
  
  TextureManager() = default;
  ~TextureManager();
  
  std::unordered_map<TextureSettings, Texture*, TextureHash> loadedTextures;
};


/// Convenience class to load textures.
/// If you expect that the application might try to load the texture multiple times,
/// load it via the TextureManager instead.
class Texture {
 public:
  /// Creates an invalid texture.
  Texture() = default;
  
  /// Frees the texture memory on the GPU.
  ~Texture();
  
  /// Loads the texture from the given QImage into GPU memory. The image can be released afterwards.
  void Load(const QImage& image, int wrapMode, int magFilter, int minFilter);
  
  /// Loads the texture from the given file. Returns true on success, false on failure.
  /// The file is assumed to have 8 bits per color channel, with 4 channels in total.
  bool Load(const std::filesystem::path& path, int wrapMode, int magFilter, int minFilter);
  
  /// Returns the OpenGL texture Id.
  GLuint GetId() const { return textureId; }
  
  int GetWidth() const { return width; }
  int GetHeight() const { return height; }
  
  inline void AddReference() { ++ referenceCount; }
  /// Returns true if the reference count reaches zero.
  inline bool RemoveReference() { -- referenceCount; return referenceCount == 0; }
  inline int GetReferenceCount() const { return referenceCount; }
  
 private:
  /// OpenGL texture Id.
  GLuint textureId = -1;
  
  /// Width of the texture in pixels.
  int width = -1;
  
  /// Height of the texture in pixels.
  int height;
  
  /// Reference count (only to be used if the Texture is loaded via the TextureManager).
  int referenceCount = 0;
};
