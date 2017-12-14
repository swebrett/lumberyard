/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
#pragma once

namespace AssetProcessor
{
    //! Sends the job over to the builder and blocks until the response is received or the builder crashes/times out
    template<typename TNetRequest, typename TNetResponse, typename TRequest, typename TResponse>
    void Builder::RunJob(const TRequest& request, TResponse& response, AZ::u32 processTimeoutLimitInSeconds, const AZStd::string& task, const AZStd::string& modulePath, AssetBuilderSDK::JobCancelListener* jobCancelListener /*= nullptr*/) const
    {
        TNetRequest netRequest;
        TNetResponse netResponse;
        netRequest.m_request = request;

        QString tempFolderPath;

        // For debugging purposes, we write the request out to disk
        if (!DebugWriteRequestFile(tempFolderPath, request, task, modulePath))
        {
            return;
        }

        AZ::u32 type;
        QByteArray data;
        AZStd::binary_semaphore wait;

        AZ_TracePrintf(AssetProcessor::DebugChannel, "Sending job request to builder\n");

        unsigned int serial;
        AssetProcessor::ConnectionBus::EventResult(serial, m_connectionId, &AssetProcessor::ConnectionBusTraits::SendRequest, netRequest, [&](AZ::u32 msgType, QByteArray msgData)
        {
            type = msgType;
            data = msgData;
            wait.release();
        });

        if (!WaitForBuilderResponse(jobCancelListener, processTimeoutLimitInSeconds, &wait))
        {
            // Clear out the response handler so it doesn't get triggered after the variables go out of scope (also to clean up the memory)
            AssetProcessor::ConnectionBus::Event(m_connectionId, &AssetProcessor::ConnectionBusTraits::RemoveResponseHandler, serial);
            return;
        }

        AZ_Assert(type == netRequest.GetMessageType(), "Response type does not match");

        if (!AZ::Utils::LoadObjectFromBufferInPlace(data.data(), data.length(), netResponse))
        {
            AZ_Error("Builder", false, "Failed to deserialize processJobs response");
            return;
        }

        AZ_TracePrintf(AssetProcessor::DebugChannel, "Job request completed\n");

        if (netResponse.m_response.Succeeded() && s_DeleteSuccessfulJobRequestFiles)
        {
            AZ::IO::FileIOBase::GetInstance()->DestroyPath(tempFolderPath.toStdString().c_str());
        }

        response = AZStd::move(netResponse.m_response);
    }

    template<typename TRequest>
    bool Builder::DebugWriteRequestFile(QString& tempFolderPath, const TRequest& request, const AZStd::string& task, const AZStd::string& modulePath) const
    {
        if (!AssetUtilities::CreateTempWorkspace(tempFolderPath))
        {
            AZ_Error("Builder", false, "Failed to create temporary workspace to execute builder task");
            return false;
        }

        const QDir tempFolder = QDir(tempFolderPath);
        const AZStd::string jobRequestFile = tempFolder.filePath("request.xml").toStdString().c_str();
        const AZStd::string jobResponseFile = tempFolder.filePath("response.xml").toStdString().c_str();

        if (!AZ::Utils::SaveObjectToFile(jobRequestFile, AZ::DataStream::ST_XML, &request))
        {
            AZ_Error("Builder", false, "Failed to save request to file: %s", jobRequestFile.c_str());
            return false;
        }

        auto params = BuildParams(task.c_str(), modulePath.c_str(), "", jobRequestFile, jobResponseFile);

        AZ_TracePrintf(AssetProcessor::DebugChannel, "Job request written to %s\n", jobRequestFile.c_str());
        AZ_TracePrintf(AssetProcessor::DebugChannel, "To re-run this request manually, run AssetBuilder with the following parameters:\n");
        AZ_TracePrintf(AssetProcessor::DebugChannel, "%s\n", params.c_str());

        return true;
    }
} // namespace AssetProcessor