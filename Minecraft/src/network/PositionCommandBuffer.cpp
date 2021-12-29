#include "network/PositionCommandBuffer.h"
#include "network/Network.h"
#include "core/Scene.h"
#include "core/Components.h"

namespace Minecraft
{
	void PositionCommandBuffer::init(int inMaxNumPositionCommands)
	{
		maxSize = inMaxNumPositionCommands;
		buffer = (UpdatePositionCommand*)g_memory_allocate(sizeof(UpdatePositionCommand) * maxSize);
		size = 0;
	}

	void PositionCommandBuffer::free()
	{
		if (buffer)
		{
			g_memory_free(buffer);
			size = 0;
			buffer = nullptr;
		}
	}

	void PositionCommandBuffer::insert(const UpdatePositionCommand& command)
	{
		if (!buffer)
		{
			return;
		}

		int indexToInsertAt = -1;
		// Do binary search to find where we should put the new command based on timestamp
		int left = 0;
		int right = size;
		while (left != right)
		{
			int mid = left + ((right - left) / 2);
			if (command.timestamp < buffer[mid].timestamp)
			{
				// Search the left half
				if (right == mid)
				{
					right = glm::max(mid - 1, left);
				}
				else
				{
					right = mid;
				}
			}
			else if (command.timestamp > buffer[mid].timestamp)
			{
				if (left == mid)
				{
					left = glm::min(mid + 1, right);
				}
				else
				{
					left = mid;
				}
			}
			else
			{
				indexToInsertAt = mid;
				break;
			}

			if (right == left)
			{
				indexToInsertAt = right;
				break;
			}
		}

		// Add the command to the end of the list if there's no room for it
		if (indexToInsertAt == -1)
		{
			indexToInsertAt = size;
		}

		// Move the data as necessary to make room for the next command
		if (indexToInsertAt >= maxSize)
		{
			indexToInsertAt = maxSize - 1;
			size--;
			// Otherwise move to the left
			std::memmove(buffer, buffer + 1,
				sizeof(UpdatePositionCommand) * indexToInsertAt);
		}
		else if (indexToInsertAt < size)
		{
			if (size + 1 < maxSize)
			{
				// Move data to the right if we have room
				std::memmove(buffer + indexToInsertAt + 1, buffer + indexToInsertAt,
					sizeof(UpdatePositionCommand) * (size - indexToInsertAt));
			}
			else
			{
				if (indexToInsertAt == 0)
				{
					// If we're trying to insert an element at the beginning of the array
					// and we have no more room for new stuff, just discard this because
					// it's too old anyways and we don't care about it anymore
					return;
				}

				// Otherwise move to the left
				std::memmove(buffer, buffer + 1,
					sizeof(UpdatePositionCommand) * indexToInsertAt);
				size--;
			}
		}

		// Insert the command into the list and increment the number of commands
		g_memory_copyMem(buffer + indexToInsertAt, (void*)&command, sizeof(UpdatePositionCommand));
		size++;
	}

	glm::vec3 PositionCommandBuffer::predict(uint64 lagCompensation, Ecs::EntityId entity, bool* predictionSuccess)
	{
		*predictionSuccess = false;

		Ecs::Registry* registry = Scene::getRegistry();
		if (registry->hasComponent<Transform>(entity))
		{
			// Interpolate or extrapolate based on our commands
			uint64 minTime = Network::now() - lagCompensation;

			for (int i = size - 1; i >= 0; i--)
			{
				UpdatePositionCommand& currentUpdateCmd = (*this)[i];
				if (currentUpdateCmd.timestamp < minTime && currentUpdateCmd.entity == entity)
				{
					for (int j = i + 1; j < size; j++)
					{
						UpdatePositionCommand& nextUpdateCmd = (*this)[j];
						if (nextUpdateCmd.entity == entity)
						{
							*predictionSuccess = true;
							// Interpolate
							uint64 deltaInMs = nextUpdateCmd.timestamp - currentUpdateCmd.timestamp;
							float timeInterpolated = (float)((double)(minTime - currentUpdateCmd.timestamp) / (double)deltaInMs);
							return currentUpdateCmd.position +
								((nextUpdateCmd.position - currentUpdateCmd.position) * timeInterpolated);
						}
					}

					break;
				}
			}

			if (!(*predictionSuccess))
			{
				// Extrapolate if we have some history left
				//g_logger_warning("Extrapolation being performed. This must be a laggy network.");
				if (size > 1)
				{
					//uint64 deltaInMs = positionCommandBuffer[positionCommandBuffer.size - 1].timestamp -
					//	positionCommandBuffer[positionCommandBuffer.size - 2].timestamp;
					//float timeInterpolated = (float)((double)(minTime - positionCommandBuffer[positionCommandBuffer.size - 1].timestamp) / (double)deltaInMs);
					//position = positionCommandBuffer[positionCommandBuffer.size - 1].position +
					//	((positionCommandBuffer[positionCommandBuffer.size - 1].position - positionCommandBuffer[positionCommandBuffer.size - 2].position) *
					//		timeInterpolated);
					//foundPosition = true;
				}
				else
				{
					g_logger_error("Failed to extrapolate or interpolate the position.");
				}
			}
		}
		else
		{
			g_logger_error("Entity has no transform component.");
		}
		return glm::vec3();
	}
}