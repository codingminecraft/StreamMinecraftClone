#ifndef MINECRAFT_FRAMEBUFFER_H
#define MINECRAFT_FRAMEBUFFER_H
#include "core.h"

namespace Minecraft
{
	struct Texture;
	enum class ByteFormat;

	struct Framebuffer
	{
		uint32 fbo;
		int32 width;
		int32 height;

		// Depth/Stencil attachment (optional)
		uint32 rbo;
		ByteFormat depthStencilFormat;
		bool includeDepthStencil;

		// Color attachments
		// TODO: All color attachments will be resized to match the framebuffer size (perhaps this should be changed in the future...?)
		std::vector<Texture> colorAttachments;

		void bind() const;
		void unbind() const;
		void clearColorAttachmentUint32(int colorAttachment, uint32 clearColor) const;
		void clearColorAttachmentRgb(int colorAttachment, glm::vec3 clearColor) const;

		uint32 readPixelUint32(int colorAttachment, int x, int y) const;
		uint8* readAllPixelsRgb8(int colorAttachment) const;
		void freePixelsRgb8(uint8* pixels) const;
		const Texture& getColorAttachment(int index) const;

		void regenerate();
		void destroy(bool clearColorAttachmentSpecs = true);
	};

	class FramebufferBuilder
	{
	public:
		FramebufferBuilder(uint32 width, uint32 height);
		FramebufferBuilder& includeDepthStencilBuffer();
		Framebuffer generate();

		FramebufferBuilder& addColorAttachment(const Texture& textureSpec); // TODO: The order the attachments are added will be the index they get (change this in the future too...?)

	private:
		Framebuffer framebuffer;
	};
}

#endif