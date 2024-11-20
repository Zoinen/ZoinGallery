#ifndef IMAGEDECODERFACTORY_H
#define IMAGEDECODERFACTORY_H

#include <functional>
#include <QString>
#include <QList>

class ImageDecoderInterface;

#define REGISTER_DECODER_DECLARATION(className, decoderPriority) \
    QString decoderName() const override { return #className; } \
    static ImageDecoderInterface* create(); \
    static const bool _registered; \
    static const int _decoderPriority = decoderPriority;

#define REGISTER_DECODER_DEFINITION(className) \
    ImageDecoderInterface* className::create() { \
        return new className(); \
    } \
    const bool className::_registered = ImageDecoderFactory::registerClass(&className::create, _decoderPriority);

class ImageDecoderFactory {
public:
    using CreatorFunc = std::function<ImageDecoderInterface*()>;

    static bool registerClass(CreatorFunc creator, int priority) {
        auto it = _decoders.begin();
        while (it != _decoders.end() && it->priority >= priority) {
            ++it;
        }
        _decoders.insert(it, {creator, priority});
        return true;
    }

    static int decoderCount() {
        return _decoders.size();
    }

    static ImageDecoderInterface* createDecoder(int decoderIndex) {
        if (decoderIndex >= 0 && decoderIndex < _decoders.size()) {
            return _decoders[decoderIndex].creatorFunc();
        }
        return nullptr;
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
