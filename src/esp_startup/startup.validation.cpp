#include "esp_startup/startup.h"

bool ESPStartup::validateAndResolve(std::string& outErrorMessage) {
    if( sections.empty() ){
        outErrorMessage = "no startup sections declared";
        return false;
    }

    if( steps.empty() ){
        outErrorMessage = "no startup steps registered";
        return false;
    }

    for( size_t sectionIndex = 0; sectionIndex < sections.size(); sectionIndex++ ){
        if( sections[sectionIndex].name.empty() ){
            outErrorMessage = "startup section with empty name";
            return false;
        }
    }

    for( size_t index = 0; index < steps.size(); index++ ){
        const StepDefinition& step = steps[index];

        if( step.name.empty() ){
            outErrorMessage = "startup step with empty name";
            return false;
        }

        if( step.sectionIndex >= sections.size() ){
            outErrorMessage = "startup step has invalid section: " + step.name;
            return false;
        }

        if( !step.callback ){
            outErrorMessage = step.name + " callback missing";
            return false;
        }

        for( size_t compareIndex = index + 1; compareIndex < steps.size(); compareIndex++ ){
            if( steps[compareIndex].name == step.name ){
                outErrorMessage = "duplicate startup step name: " + step.name;
                return false;
            }
        }
    }

    for( size_t stepIndex = 0; stepIndex < steps.size(); stepIndex++ ){
        const StepDefinition& step = steps[stepIndex];

        for( size_t dependencyIndex = 0; dependencyIndex < step.dependencies.size(); dependencyIndex++ ){
            const std::string& dependencyName = step.dependencies[dependencyIndex];

            if( dependencyName == step.name ){
                outErrorMessage = "self dependency: " + step.name;
                return false;
            }

            if( !dependencyExists(dependencyName.c_str()) ){
                outErrorMessage = "missing dependency " + dependencyName + " for " + step.name;
                return false;
            }

            for( size_t lookupIndex = 0; lookupIndex < steps.size(); lookupIndex++ ){
                if( steps[lookupIndex].name != dependencyName ){
                    continue;
                }

                if( sectionRank(steps[lookupIndex].sectionIndex) > sectionRank(step.sectionIndex) ){
                    outErrorMessage =
                        "dependency section ordering invalid for " + step.name + " -> " + dependencyName;
                    return false;
                }
                break;
            }
        }
    }

    sectionBatches.clear();
    sectionBatches.resize(sections.size());

    for( size_t sectionIndex = 0; sectionIndex < sections.size(); sectionIndex++ ){
        if( !buildSectionBatches(sectionIndex, sectionBatches[sectionIndex], outErrorMessage) ){
            return false;
        }
    }

    return true;
}

bool ESPStartup::buildSectionBatches(
    size_t sectionIndex,
    std::vector<std::vector<size_t>>& outBatches,
    std::string& outErrorMessage
) const {
    std::vector<size_t> sectionSteps;
    for( size_t index = 0; index < steps.size(); index++ ){
        if( steps[index].sectionIndex == sectionIndex ){
            sectionSteps.push_back(index);
        }
    }

    std::vector<uint16_t> indegree(sectionSteps.size(), 0);
    std::vector<std::vector<size_t>> edges(sectionSteps.size());

    for( size_t sectionStepIndex = 0; sectionStepIndex < sectionSteps.size(); sectionStepIndex++ ){
        const size_t stepIndex = sectionSteps[sectionStepIndex];
        const StepDefinition& step = steps[stepIndex];

        for( size_t dependencyIndex = 0; dependencyIndex < step.dependencies.size(); dependencyIndex++ ){
            const std::string& dependencyName = step.dependencies[dependencyIndex];

            for( size_t lookupSectionStepIndex = 0; lookupSectionStepIndex < sectionSteps.size(); lookupSectionStepIndex++ ){
                const size_t candidateStepIndex = sectionSteps[lookupSectionStepIndex];
                if( steps[candidateStepIndex].name != dependencyName ){
                    continue;
                }

                edges[lookupSectionStepIndex].push_back(sectionStepIndex);
                indegree[sectionStepIndex]++;
                break;
            }
        }
    }

    std::vector<size_t> ready;
    ready.reserve(sectionSteps.size());
    for( size_t sectionStepIndex = 0; sectionStepIndex < sectionSteps.size(); sectionStepIndex++ ){
        if( indegree[sectionStepIndex] == 0 ){
            ready.push_back(sectionStepIndex);
        }
    }

    outBatches.clear();
    outBatches.reserve(sectionSteps.size());

    size_t readyCursor = 0;
    while( readyCursor < ready.size() ){
        const size_t waveEnd = ready.size();
        std::vector<size_t> batch;
        batch.reserve(waveEnd - readyCursor);

        while( readyCursor < waveEnd ){
            const size_t currentSectionStepIndex = ready[readyCursor++];
            batch.push_back(sectionSteps[currentSectionStepIndex]);

            for( size_t edgeIndex = 0; edgeIndex < edges[currentSectionStepIndex].size(); edgeIndex++ ){
                const size_t nextSectionStepIndex = edges[currentSectionStepIndex][edgeIndex];
                indegree[nextSectionStepIndex]--;
                if( indegree[nextSectionStepIndex] == 0 ){
                    ready.push_back(nextSectionStepIndex);
                }
            }
        }

        outBatches.push_back(batch);
    }

    size_t resolvedSteps = 0;
    for( size_t batchIndex = 0; batchIndex < outBatches.size(); batchIndex++ ){
        resolvedSteps += outBatches[batchIndex].size();
    }

    if( resolvedSteps != sectionSteps.size() ){
        outErrorMessage = "startup dependency cycle in section: " + sections[sectionIndex].name;
        return false;
    }

    return true;
}

bool ESPStartup::dependencyExists(const char* dependencyName) const {
    if( dependencyName == nullptr ){
        return false;
    }

    for( size_t index = 0; index < steps.size(); index++ ){
        if( steps[index].name == dependencyName ){
            return true;
        }
    }

    return false;
}

int ESPStartup::sectionRank(size_t sectionIndex) const {
    return static_cast<int>(sectionIndex);
}
