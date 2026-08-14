#ifndef IMAGEDECODERFACTORY_H
#define IMAGEDECODERFACTORY_H

#include <functional>
#include <memory>
#include <QString>
#include <QList>

class ImageDecoderInterface;

#define REGISTER_DECODER_DECLARATION(className, decoderPriority) \
public: \
    QString decoderName() const override { return #className; } \
    static ImageDecoderInterface* create(); \
    static constexpr int _decoderPriority = decoderPriority;

#define REGISTER_DECODER_DEFINITION(className) \
    ImageDecoderInterface* className::create() { \
        return new className(); \
    }

class ImageDecoderFactory {
public:
    using CreatorFunc = std::function<ImageDecoderInterface*()>;

    // References every built-in decoder explicitly. This is intentionally not
    // a static initializer: ZoinGalleryCore is a static archive and otherwise
    // linkers are free to discard decoder-only translation units.
    static void registerBuiltInDecoders();

    static bool registerClass(CreatorFunc creator, int priority) {
        auto it = _decoders.begin();
        while (it != _decoders.end() && it->priority >= priority) {
            ++it;
        }
        _decoders.insert(it, {creator, priority});
        return true;
    }

    static int decoderCount() {
        registerBuiltInDecoders();
        return _decoders.size();
    }

    static std::unique_ptr<ImageDecoderInterface> createDecoder(
        int decoderIndex) {
        registerBuiltInDecoders();
        if (decoderIndex >= 0 && decoderIndex < _decoders.size()) {
            return std::unique_ptr<ImageDecoderInterface>(
                _decoders[decoderIndex].creatorFunc());
        }
        return {};
    }

private:
    // Private constructor to enforce singleton pattern
    ImageDecoderFactory() {}

    struct Decoder {
        CreatorFunc creatorFunc;
        int priority;
    };

    static QList<Decoder> _decoders;
};

#endif // IMAGEDECODERFACTORY_H
