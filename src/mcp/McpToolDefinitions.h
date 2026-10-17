#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

inline QJsonArray getMcpAllToolDefinitions()
{
    QJsonArray tools;

    // 1. get_project_summary
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_project_summary");
        t[QStringLiteral("description")] = QStringLiteral("Retrieves summary of the active Ensemble project including title, author, chapter count, total word count, and unsaved changes status.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 2. list_chapters
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("list_chapters");
        t[QStringLiteral("description")] = QStringLiteral("Lists all chapters in the active project with their 0-based index, title, order, and word count.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 3. get_chapter
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_chapter");
        t[QStringLiteral("description")] = QStringLiteral("Gets the HTML and/or plain text content of a specific chapter.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject idxProp;
        idxProp[QStringLiteral("type")] = QStringLiteral("integer");
        idxProp[QStringLiteral("description")] = QStringLiteral("The 0-based index of the chapter to retrieve.");
        props[QStringLiteral("chapterIndex")] = idxProp;
        QJsonObject formatProp;
        formatProp[QStringLiteral("type")] = QStringLiteral("string");
        formatProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("all"), QStringLiteral("html"), QStringLiteral("text")};
        formatProp[QStringLiteral("description")] = QStringLiteral("Optional format: 'all' (default, returns html and plainText), 'html' (HTML only), or 'text' (plain text only).");
        props[QStringLiteral("format")] = formatProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("chapterIndex")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 4. update_chapter_html
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("update_chapter_html");
        t[QStringLiteral("description")] = QStringLiteral("Updates the HTML content of a chapter. The HTML is automatically sanitized to match AO3 whitelist standards and the live preview/editors are refreshed immediately.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject idxProp;
        idxProp[QStringLiteral("type")] = QStringLiteral("integer");
        idxProp[QStringLiteral("description")] = QStringLiteral("The 0-based index of the chapter to update.");
        props[QStringLiteral("chapterIndex")] = idxProp;
        QJsonObject htmlProp;
        htmlProp[QStringLiteral("type")] = QStringLiteral("string");
        htmlProp[QStringLiteral("description")] = QStringLiteral("The new HTML content for the chapter.");
        props[QStringLiteral("html")] = htmlProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("chapterIndex"), QStringLiteral("html")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 5. add_chapter
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("add_chapter");
        t[QStringLiteral("description")] = QStringLiteral("Creates and appends a new chapter to the project.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject titleProp;
        titleProp[QStringLiteral("type")] = QStringLiteral("string");
        titleProp[QStringLiteral("description")] = QStringLiteral("Optional title for the new chapter.");
        props[QStringLiteral("title")] = titleProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 6. delete_chapter
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("delete_chapter");
        t[QStringLiteral("description")] = QStringLiteral("Deletes a chapter from the project by its 0-based index.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject idxProp;
        idxProp[QStringLiteral("type")] = QStringLiteral("integer");
        idxProp[QStringLiteral("description")] = QStringLiteral("The 0-based index of the chapter to delete.");
        props[QStringLiteral("chapterIndex")] = idxProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("chapterIndex")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 7. reorder_chapters
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("reorder_chapters");
        t[QStringLiteral("description")] = QStringLiteral("Moves a chapter from one index position to another.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject fromProp;
        fromProp[QStringLiteral("type")] = QStringLiteral("integer");
        fromProp[QStringLiteral("description")] = QStringLiteral("The 0-based starting index.");
        props[QStringLiteral("fromIndex")] = fromProp;
        QJsonObject toProp;
        toProp[QStringLiteral("type")] = QStringLiteral("integer");
        toProp[QStringLiteral("description")] = QStringLiteral("The 0-based destination index.");
        props[QStringLiteral("toIndex")] = toProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("fromIndex"), QStringLiteral("toIndex")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 8. get_workskin_css
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_workskin_css");
        t[QStringLiteral("description")] = QStringLiteral("Retrieves the custom AO3 Work Skin CSS of the active project.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 9. set_workskin_css
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("set_workskin_css");
        t[QStringLiteral("description")] = QStringLiteral("Validates and updates the custom AO3 Work Skin CSS for the project, triggering a live Chromium re-render. Rejects malformed CSS syntax unless allowMalformed is set to true.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject cssProp;
        cssProp[QStringLiteral("type")] = QStringLiteral("string");
        cssProp[QStringLiteral("description")] = QStringLiteral("The CSS stylesheet rules.");
        props[QStringLiteral("css")] = cssProp;
        QJsonObject malProp;
        malProp[QStringLiteral("type")] = QStringLiteral("boolean");
        malProp[QStringLiteral("description")] = QStringLiteral("Optional flag to bypass syntax validation and allow malformed CSS (default: false).");
        props[QStringLiteral("allowMalformed")] = malProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("css")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 10. get_problems
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_problems");
        t[QStringLiteral("description")] = QStringLiteral("Returns the current diagnostic lint errors and warnings (AO3 tag whitelist violations, unclosed tags, CSS validation issues).");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 11. load_project
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("load_project");
        t[QStringLiteral("description")] = QStringLiteral("Loads an .ao3proj story archive from disk into Ensemble, updating chapters, stylesheet, tags, lore graph, and git history.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject fileProp;
        fileProp[QStringLiteral("type")] = QStringLiteral("string");
        fileProp[QStringLiteral("description")] = QStringLiteral("Absolute path to the .ao3proj project file to open.");
        props[QStringLiteral("filePath")] = fileProp;
        QJsonObject saveProp;
        saveProp[QStringLiteral("type")] = QStringLiteral("boolean");
        saveProp[QStringLiteral("description")] = QStringLiteral("Whether to save any active modified project before loading (default: false).");
        props[QStringLiteral("saveCurrent")] = saveProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("filePath")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 12. save_project
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("save_project");
        t[QStringLiteral("description")] = QStringLiteral("Saves the active project to disk as an .ao3proj archive. When filePath is provided, it switches the active project to that path (standard 'Save As' behavior) unless saveCopy is true, which exports a standalone snapshot without altering Ensemble's currently open project.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject pathProp;
        pathProp[QStringLiteral("type")] = QStringLiteral("string");
        pathProp[QStringLiteral("description")] = QStringLiteral("Optional target .ao3proj file path. If omitted, uses the current project file path.");
        props[QStringLiteral("filePath")] = pathProp;
        QJsonObject copyProp;
        copyProp[QStringLiteral("type")] = QStringLiteral("boolean");
        copyProp[QStringLiteral("description")] = QStringLiteral("If true, exports a project copy/snapshot to 'filePath' without switching Ensemble's active project to that file and without resetting dirty status. Defaults to false (Save As behavior).");
        props[QStringLiteral("saveCopy")] = copyProp;
        QJsonObject msgProp;
        msgProp[QStringLiteral("type")] = QStringLiteral("string");
        msgProp[QStringLiteral("description")] = QStringLiteral("Optional git-like commit message describing the modifications saved into history.cmt.");
        props[QStringLiteral("commitMessage")] = msgProp;
        QJsonObject msgAliasProp;
        msgAliasProp[QStringLiteral("type")] = QStringLiteral("string");
        msgAliasProp[QStringLiteral("description")] = QStringLiteral("Alias for commitMessage.");
        props[QStringLiteral("message")] = msgAliasProp;
        QJsonObject authProp;
        authProp[QStringLiteral("type")] = QStringLiteral("string");
        authProp[QStringLiteral("description")] = QStringLiteral("Optional commit author name (defaults to calling AI model name or project author).");
        props[QStringLiteral("author")] = authProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 13. get_project_history
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_project_history");
        t[QStringLiteral("description")] = QStringLiteral("Retrieves the git-like version history from history.cmt, parsed into structured commit objects (hash, author, date, message) and raw log text.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject limitProp;
        limitProp[QStringLiteral("type")] = QStringLiteral("integer");
        limitProp[QStringLiteral("description")] = QStringLiteral("Maximum number of commits to return (default: 50).");
        props[QStringLiteral("limit")] = limitProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 14. add_history_commit
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("add_history_commit");
        t[QStringLiteral("description")] = QStringLiteral("Records a new git-like commit entry into history.cmt for the project with a unique commit hash, author, ISO timestamp, and message.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject msgProp;
        msgProp[QStringLiteral("type")] = QStringLiteral("string");
        msgProp[QStringLiteral("description")] = QStringLiteral("Commit message describing changes made to the story.");
        props[QStringLiteral("message")] = msgProp;
        QJsonObject authProp;
        authProp[QStringLiteral("type")] = QStringLiteral("string");
        authProp[QStringLiteral("description")] = QStringLiteral("Author name for this commit (defaults to calling agent).");
        props[QStringLiteral("author")] = authProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("message")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 15. test_firewall_connection
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("test_firewall_connection");
        t[QStringLiteral("description")] = QStringLiteral("Runs a diagnostic self-test connecting to the local MCP WebSocket server to verify loopback socket reachability and identify firewall or proxy blocks.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 16. reload_project
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("reload_project");
        t[QStringLiteral("description")] = QStringLiteral("Reloads the active project from disk, discarding any unsaved changes and resetting isDirty to false.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 17. get_preview_screenshot
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_preview_screenshot");
        t[QStringLiteral("description")] = QStringLiteral("Renders a chapter in Chromium with active AO3 Work Skin styling and returns a base64-encoded PNG screenshot as an MCP image content block.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject idxProp;
        idxProp[QStringLiteral("type")] = QStringLiteral("integer");
        idxProp[QStringLiteral("description")] = QStringLiteral("Optional 0-based index of the chapter to preview. Defaults to the active chapter.");
        props[QStringLiteral("chapterIndex")] = idxProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 18. get_agent_presence
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_agent_presence");
        t[QStringLiteral("description")] = QStringLiteral("Inspects all active AI agents currently connected to Ensemble (e.g. Gemini Antigravity, Claude Desktop/Code) for multi-agent presence awareness, co-working, and peer synchronization.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 19. register_agent_presence
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("register_agent_presence");
        t[QStringLiteral("description")] = QStringLiteral("Announces or updates your AI agent presence, identity, role (co-author, reviewer, proofreader, etc.), and current working note so other connected AI models can coordinate and co-work.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject nameProp;
        nameProp[QStringLiteral("type")] = QStringLiteral("string");
        nameProp[QStringLiteral("description")] = QStringLiteral("The name of your AI agent or model (e.g. 'antigravity', 'claude', 'gemini').");
        props[QStringLiteral("agentName")] = nameProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Specific AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        QJsonObject roleProp;
        roleProp[QStringLiteral("type")] = QStringLiteral("string");
        roleProp[QStringLiteral("description")] = QStringLiteral("Your role in the session (e.g. 'co-author', 'reviewer', 'editor', 'proofreader', 'architect'). Defaults to 'co-author'.");
        props[QStringLiteral("role")] = roleProp;
        QJsonObject noteProp;
        noteProp[QStringLiteral("type")] = QStringLiteral("string");
        noteProp[QStringLiteral("description")] = QStringLiteral("A brief note describing what you are currently focusing on or working on.");
        props[QStringLiteral("workingNote")] = noteProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = true;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 20. get_agentic_summary
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_agentic_summary");
        t[QStringLiteral("description")] = QStringLiteral("Retrieves storage quotas (5 MB shared, 20 MB localized), current byte usage, index metadata, list of shared files, and registered localized agent directories in .ao3proj agentic-datas.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        schema[QStringLiteral("properties")] = QJsonObject();
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 21. read_agentic_file
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("read_agentic_file");
        t[QStringLiteral("description")] = QStringLiteral("Reads a file from the .ao3proj agentic-datas repository (from the coroutine 'shared/' directory or an agent's private '<agent>/' memory directory).");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject pathProp;
        pathProp[QStringLiteral("type")] = QStringLiteral("string");
        pathProp[QStringLiteral("description")] = QStringLiteral("Relative path of the file to read (e.g. 'plot_notes.md', 'character_sheet.json').");
        props[QStringLiteral("path")] = pathProp;
        QJsonObject scopeProp;
        scopeProp[QStringLiteral("type")] = QStringLiteral("string");
        scopeProp[QStringLiteral("description")] = QStringLiteral("Storage scope: 'shared' (default) or agent name (e.g. 'claude', 'antigravity', 'cursor').");
        props[QStringLiteral("scope")] = scopeProp;
        QJsonObject encProp;
        encProp[QStringLiteral("type")] = QStringLiteral("string");
        encProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("utf-8"), QStringLiteral("base64")};
        encProp[QStringLiteral("description")] = QStringLiteral("Optional encoding: 'utf-8' (default) or 'base64'.");
        props[QStringLiteral("encoding")] = encProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("path")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 22. write_agentic_file
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("write_agentic_file");
        t[QStringLiteral("description")] = QStringLiteral("Writes or updates a file in .ao3proj agentic-datas ('shared/' for coroutine multi-agent collaboration or '<agent>/' for private memory). Strictly enforces the 5 MB shared cap and 20 MB localized cap.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject pathProp;
        pathProp[QStringLiteral("type")] = QStringLiteral("string");
        pathProp[QStringLiteral("description")] = QStringLiteral("Relative file path (e.g. 'outline.md', 'draft_notes.txt').");
        props[QStringLiteral("path")] = pathProp;
        QJsonObject contentProp;
        contentProp[QStringLiteral("type")] = QStringLiteral("string");
        contentProp[QStringLiteral("description")] = QStringLiteral("The content of the file to write.");
        props[QStringLiteral("content")] = contentProp;
        QJsonObject scopeProp;
        scopeProp[QStringLiteral("type")] = QStringLiteral("string");
        scopeProp[QStringLiteral("description")] = QStringLiteral("Target storage scope: 'shared' (default) or private agent name (e.g. 'claude', 'antigravity', 'cursor').");
        props[QStringLiteral("scope")] = scopeProp;
        QJsonObject encProp;
        encProp[QStringLiteral("type")] = QStringLiteral("string");
        encProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("utf-8"), QStringLiteral("base64")};
        encProp[QStringLiteral("description")] = QStringLiteral("Optional content encoding: 'utf-8' (default) or 'base64'.");
        props[QStringLiteral("encoding")] = encProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("path"), QStringLiteral("content")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 23. append_agent_history
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("append_agent_history");
        t[QStringLiteral("description")] = QStringLiteral("Appends a structured history entry to agent_model_history.xml in .ao3proj recording an AI model interaction, analysis, suggestion, or co-writing milestone.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject sumProp;
        sumProp[QStringLiteral("type")] = QStringLiteral("string");
        sumProp[QStringLiteral("description")] = QStringLiteral("Detailed summary of the action or contribution performed by the model.");
        props[QStringLiteral("summary")] = sumProp;
        QJsonObject agentProp;
        agentProp[QStringLiteral("type")] = QStringLiteral("string");
        agentProp[QStringLiteral("description")] = QStringLiteral("Optional agent name (e.g. 'antigravity', 'claude').");
        props[QStringLiteral("agentName")] = agentProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional model family/version (e.g. 'gemini-2.5-pro', 'claude-3-7-sonnet').");
        props[QStringLiteral("model")] = modelProp;
        QJsonObject roleProp;
        roleProp[QStringLiteral("type")] = QStringLiteral("string");
        roleProp[QStringLiteral("description")] = QStringLiteral("Optional role (e.g. 'co-author', 'reviewer', 'editor').");
        props[QStringLiteral("role")] = roleProp;
        QJsonObject actProp;
        actProp[QStringLiteral("type")] = QStringLiteral("string");
        actProp[QStringLiteral("description")] = QStringLiteral("Optional action type (e.g. 'sync', 'review', 'co-write', 'refactor').");
        props[QStringLiteral("action")] = actProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("summary")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 24. post_agent_thought
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("post_agent_thought");
        t[QStringLiteral("description")] = QStringLiteral("Shares an AI model's internal thoughts, critique, reasoning, or co-writing plans to be displayed live in Ensemble's Agent Thoughts Monitor UI.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject thoughtProp;
        thoughtProp[QStringLiteral("type")] = QStringLiteral("string");
        thoughtProp[QStringLiteral("description")] = QStringLiteral("The internal reasoning, critique, suggestions, or thoughts about the story.");
        props[QStringLiteral("thought")] = thoughtProp;
        QJsonObject catProp;
        catProp[QStringLiteral("type")] = QStringLiteral("string");
        catProp[QStringLiteral("description")] = QStringLiteral("Optional category: 'critique', 'analysis', 'plan', 'reflection', 'pacing', or 'worldbuilding'.");
        props[QStringLiteral("category")] = catProp;
        QJsonObject targetProp;
        targetProp[QStringLiteral("type")] = QStringLiteral("string");
        targetProp[QStringLiteral("description")] = QStringLiteral("Optional target element (e.g. 'Chapter 1', 'Work Skin', 'Character: Alice').");
        props[QStringLiteral("target")] = targetProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("thought")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 25. get_agent_history
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_agent_history");
        t[QStringLiteral("description")] = QStringLiteral("Retrieves the audit trail and history of changes made to the project by all AI models and sessions, including timestamps, model names, actions, word-count deltas, and summaries. Also returns the latest change made by each model.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject limitProp;
        limitProp[QStringLiteral("type")] = QStringLiteral("integer");
        limitProp[QStringLiteral("description")] = QStringLiteral("Maximum number of recent history entries to return (default: 50).");
        props[QStringLiteral("limit")] = limitProp;
        QJsonObject agentProp;
        agentProp[QStringLiteral("type")] = QStringLiteral("string");
        agentProp[QStringLiteral("description")] = QStringLiteral("Optional agent name to filter history (e.g. 'claude', 'antigravity').");
        props[QStringLiteral("agentName")] = agentProp;
        QJsonObject actionProp;
        actionProp[QStringLiteral("type")] = QStringLiteral("string");
        actionProp[QStringLiteral("description")] = QStringLiteral("Optional action filter (e.g. 'update_chapter_html', 'set_workskin_css').");
        props[QStringLiteral("action")] = actionProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 26. get_lore_graph
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_lore_graph");
        t[QStringLiteral("description")] = QStringLiteral("Retrieves the story lore and narrative graph (from lore/graphs/data.grph) in custom .grph DSL syntax and structured JSON. Includes complete language grammar specification guiding AI models on how to compose .grph lines.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject nameProp;
        nameProp[QStringLiteral("type")] = QStringLiteral("string");
        nameProp[QStringLiteral("description")] = QStringLiteral("Graph name (default: 'data').");
        props[QStringLiteral("name")] = nameProp;
        QJsonObject grammarProp;
        grammarProp[QStringLiteral("type")] = QStringLiteral("boolean");
        grammarProp[QStringLiteral("description")] = QStringLiteral("Whether to include the full .grph language grammar guide (default: true).");
        props[QStringLiteral("includeGrammar")] = grammarProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 27. update_lore_graph
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("update_lore_graph");
        t[QStringLiteral("description")] = QStringLiteral("Replaces or updates the .grph narrative graph DSL. Strictly adheres to unique 8-byte magic header \\x89GRPH\\x01\\r\\n and grammar rules. Validates syntax and semantic rules: valid node types, shapes, colors, connector styles, and ensures connector and box references point to defined nodes.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject dslProp;
        dslProp[QStringLiteral("type")] = QStringLiteral("string");
        dslProp[QStringLiteral("description")] = QStringLiteral("The complete .grph DSL source code to compile into the story's narrative graph.");
        props[QStringLiteral("dsl")] = dslProp;
        QJsonObject nameProp;
        nameProp[QStringLiteral("type")] = QStringLiteral("string");
        nameProp[QStringLiteral("description")] = QStringLiteral("Graph name (default: 'data').");
        props[QStringLiteral("name")] = nameProp;
        QJsonObject layoutProp;
        layoutProp[QStringLiteral("type")] = QStringLiteral("boolean");
        layoutProp[QStringLiteral("description")] = QStringLiteral("Whether to trigger automatic layout positioning (default: false).");
        props[QStringLiteral("autoLayout")] = layoutProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("dsl")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 28. add_lore_node
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("add_lore_node");
        t[QStringLiteral("description")] = QStringLiteral("Adds or updates an individual node block in the story's lore graph with position pos(x, y), shape, color, and optional link to lore/lore_data.json.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject idProp;
        idProp[QStringLiteral("type")] = QStringLiteral("string");
        idProp[QStringLiteral("description")] = QStringLiteral("Unique node identifier (e.g. 'hero_mc', 'inciting_incident', 'ancient_relic').");
        props[QStringLiteral("id")] = idProp;
        QJsonObject labelProp;
        labelProp[QStringLiteral("type")] = QStringLiteral("string");
        labelProp[QStringLiteral("description")] = QStringLiteral("Display label of the node.");
        props[QStringLiteral("label")] = labelProp;
        QJsonObject typeProp;
        typeProp[QStringLiteral("type")] = QStringLiteral("string");
        typeProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("character"), QStringLiteral("location"), QStringLiteral("event"),
                                                      QStringLiteral("item"), QStringLiteral("faction"), QStringLiteral("concept"), QStringLiteral("plot_point")};
        typeProp[QStringLiteral("description")] = QStringLiteral("Narrative entity type.");
        props[QStringLiteral("type")] = typeProp;
        QJsonObject xProp;
        xProp[QStringLiteral("type")] = QStringLiteral("number");
        xProp[QStringLiteral("description")] = QStringLiteral("X coordinate on the visual canvas.");
        props[QStringLiteral("x")] = xProp;
        QJsonObject yProp;
        yProp[QStringLiteral("type")] = QStringLiteral("number");
        yProp[QStringLiteral("description")] = QStringLiteral("Y coordinate on the visual canvas.");
        props[QStringLiteral("y")] = yProp;
        QJsonObject shapeProp;
        shapeProp[QStringLiteral("type")] = QStringLiteral("string");
        shapeProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("rounded"), QStringLiteral("box"), QStringLiteral("diamond"),
                                                       QStringLiteral("ellipse"), QStringLiteral("cylinder"), QStringLiteral("hexagon")};
        props[QStringLiteral("shape")] = shapeProp;
        QJsonObject colProp;
        colProp[QStringLiteral("type")] = QStringLiteral("string");
        colProp[QStringLiteral("description")] = QStringLiteral("Color hex (e.g. '#3b82f6', '#f59e0b', '#10b981').");
        props[QStringLiteral("color")] = colProp;
        QJsonObject loreIdProp;
        loreIdProp[QStringLiteral("type")] = QStringLiteral("string");
        loreIdProp[QStringLiteral("description")] = QStringLiteral("Optional entity ID link in lore/lore_data.json.");
        props[QStringLiteral("loreId")] = loreIdProp;
        QJsonObject notesProp;
        notesProp[QStringLiteral("type")] = QStringLiteral("string");
        notesProp[QStringLiteral("description")] = QStringLiteral("Optional notes or narrative follow-up hooks.");
        props[QStringLiteral("notes")] = notesProp;
        QJsonObject gNameProp;
        gNameProp[QStringLiteral("type")] = QStringLiteral("string");
        gNameProp[QStringLiteral("description")] = QStringLiteral("Graph name (default: 'data').");
        props[QStringLiteral("graphName")] = gNameProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("id"), QStringLiteral("label")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 29. connect_lore_nodes
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("connect_lore_nodes");
        t[QStringLiteral("description")] = QStringLiteral("Creates or updates a relationship connector between two lore nodes with directional arrows (->, <->, --, -x) and relation labels.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject fromProp;
        fromProp[QStringLiteral("type")] = QStringLiteral("string");
        fromProp[QStringLiteral("description")] = QStringLiteral("Source node ID.");
        props[QStringLiteral("fromNodeId")] = fromProp;
        QJsonObject toProp;
        toProp[QStringLiteral("type")] = QStringLiteral("string");
        toProp[QStringLiteral("description")] = QStringLiteral("Target node ID.");
        props[QStringLiteral("toNodeId")] = toProp;
        QJsonObject arrowProp;
        arrowProp[QStringLiteral("type")] = QStringLiteral("string");
        arrowProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("->"), QStringLiteral("<->"), QStringLiteral("--"), QStringLiteral("-x")};
        arrowProp[QStringLiteral("description")] = QStringLiteral("Arrow style: '->' (directed), '<->' (bidirectional), '--' (undirected), '-x' (conflict/antagonism).");
        props[QStringLiteral("arrow")] = arrowProp;
        QJsonObject lblProp;
        lblProp[QStringLiteral("type")] = QStringLiteral("string");
        lblProp[QStringLiteral("description")] = QStringLiteral("Relationship label (e.g. 'triggers', 'mentors', 'opposes', 'located in').");
        props[QStringLiteral("label")] = lblProp;
        QJsonObject styleProp;
        styleProp[QStringLiteral("type")] = QStringLiteral("string");
        styleProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("solid"), QStringLiteral("dashed"), QStringLiteral("dotted"), QStringLiteral("thick")};
        props[QStringLiteral("style")] = styleProp;
        QJsonObject colProp;
        colProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("color")] = colProp;
        QJsonObject gNameProp;
        gNameProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("graphName")] = gNameProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("fromNodeId"), QStringLiteral("toNodeId")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 30. move_lore_node
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("move_lore_node");
        t[QStringLiteral("description")] = QStringLiteral("Moves a node block in the story's visual lore graph to new coordinates pos(x, y).");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject idProp;
        idProp[QStringLiteral("type")] = QStringLiteral("string");
        idProp[QStringLiteral("description")] = QStringLiteral("The node ID to move.");
        props[QStringLiteral("nodeId")] = idProp;
        QJsonObject xProp;
        xProp[QStringLiteral("type")] = QStringLiteral("number");
        xProp[QStringLiteral("description")] = QStringLiteral("New X coordinate on the canvas.");
        props[QStringLiteral("x")] = xProp;
        QJsonObject yProp;
        yProp[QStringLiteral("type")] = QStringLiteral("number");
        yProp[QStringLiteral("description")] = QStringLiteral("New Y coordinate on the canvas.");
        props[QStringLiteral("y")] = yProp;
        QJsonObject gNameProp;
        gNameProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("graphName")] = gNameProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("nodeId"), QStringLiteral("x"), QStringLiteral("y")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 31. get_lore_data
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("get_lore_data");
        t[QStringLiteral("description")] = QStringLiteral("Retrieves narrative lore data from the .ao3proj lore/ directory files: lore_data.json, history.json, conflicts.json, resolved.json, plans.json, references.json, and metadata.xml.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject secProp;
        secProp[QStringLiteral("type")] = QStringLiteral("string");
        secProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("all"), QStringLiteral("entities"), QStringLiteral("history"),
                                                     QStringLiteral("conflicts"), QStringLiteral("resolved"), QStringLiteral("plans"), QStringLiteral("references")};
        secProp[QStringLiteral("description")] = QStringLiteral("Which lore section to return (default: 'all').");
        props[QStringLiteral("section")] = secProp;
        QJsonObject qProp;
        qProp[QStringLiteral("type")] = QStringLiteral("string");
        qProp[QStringLiteral("description")] = QStringLiteral("Optional query to filter entities/events/conflicts by text.");
        props[QStringLiteral("query")] = qProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 32. manage_lore_data
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("manage_lore_data");
        t[QStringLiteral("description")] = QStringLiteral("Performs CRUD actions on story lore entities, timeline history, plot conflicts & resolutions, and narrative follow-up plans in the .ao3proj archive.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject actProp;
        actProp[QStringLiteral("type")] = QStringLiteral("string");
        actProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("add_entity"), QStringLiteral("update_entity"), QStringLiteral("remove_entity"),
                                                     QStringLiteral("add_history"), QStringLiteral("remove_history"),
                                                     QStringLiteral("add_conflict"), QStringLiteral("resolve_conflict"),
                                                     QStringLiteral("add_plan"), QStringLiteral("update_plan"), QStringLiteral("remove_plan"),
                                                     QStringLiteral("add_reference"), QStringLiteral("remove_reference"),
                                                     QStringLiteral("remove_graph")};
        actProp[QStringLiteral("description")] = QStringLiteral("The action to perform on the story lore database.");
        props[QStringLiteral("action")] = actProp;
        QJsonObject idProp;
        idProp[QStringLiteral("type")] = QStringLiteral("string");
        idProp[QStringLiteral("description")] = QStringLiteral("Identifier for entity, event, conflict, plan, reference, or graph.");
        props[QStringLiteral("id")] = idProp;
        QJsonObject nameProp;
        nameProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("name")] = nameProp;
        QJsonObject graphNameProp;
        graphNameProp[QStringLiteral("type")] = QStringLiteral("string");
        graphNameProp[QStringLiteral("description")] = QStringLiteral("Name of the graph to remove (alias for 'name').");
        props[QStringLiteral("graphName")] = graphNameProp;
        QJsonObject catProp;
        catProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("category")] = catProp;
        QJsonObject descProp;
        descProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("description")] = descProp;
        QJsonObject sumProp;
        sumProp[QStringLiteral("type")] = QStringLiteral("string");
        sumProp[QStringLiteral("description")] = QStringLiteral("Summary of historical event or lore entry.");
        props[QStringLiteral("summary")] = sumProp;
        QJsonObject impProp;
        impProp[QStringLiteral("type")] = QStringLiteral("string");
        impProp[QStringLiteral("description")] = QStringLiteral("Impact or narrative significance of historical event.");
        props[QStringLiteral("impact")] = impProp;
        QJsonObject srcProp;
        srcProp[QStringLiteral("type")] = QStringLiteral("string");
        srcProp[QStringLiteral("description")] = QStringLiteral("Source title or canon origin for reference.");
        props[QStringLiteral("source")] = srcProp;
        QJsonObject citeProp;
        citeProp[QStringLiteral("type")] = QStringLiteral("string");
        citeProp[QStringLiteral("description")] = QStringLiteral("Citation, episode, chapter, or URL for reference.");
        props[QStringLiteral("citation")] = citeProp;
        QJsonObject appProp;
        appProp[QStringLiteral("type")] = QStringLiteral("string");
        appProp[QStringLiteral("description")] = QStringLiteral("Applicability of reference to current work.");
        props[QStringLiteral("applicability")] = appProp;
        QJsonObject notesProp;
        notesProp[QStringLiteral("type")] = QStringLiteral("string");
        notesProp[QStringLiteral("description")] = QStringLiteral("Notes or additional details.");
        props[QStringLiteral("notes")] = notesProp;
        QJsonObject threadProp;
        threadProp[QStringLiteral("type")] = QStringLiteral("string");
        threadProp[QStringLiteral("description")] = QStringLiteral("Lore thread or plot arc for plan.");
        props[QStringLiteral("loreThread")] = threadProp;
        QJsonObject tagsProp;
        tagsProp[QStringLiteral("type")] = QStringLiteral("array");
        tagsProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("tags")] = tagsProp;
        QJsonObject titleProp;
        titleProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("title")] = titleProp;
        QJsonObject eraProp;
        eraProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("eraOrDate")] = eraProp;
        QJsonObject chProp;
        chProp[QStringLiteral("type")] = QStringLiteral("integer");
        props[QStringLiteral("storyChapter")] = chProp;
        QJsonObject sevProp;
        sevProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("severity")] = sevProp;
        QJsonObject chAProp;
        chAProp[QStringLiteral("type")] = QStringLiteral("integer");
        props[QStringLiteral("chapterA")] = chAProp;
        QJsonObject chBProp;
        chBProp[QStringLiteral("type")] = QStringLiteral("integer");
        props[QStringLiteral("chapterB")] = chBProp;
        QJsonObject cIdProp;
        cIdProp[QStringLiteral("type")] = QStringLiteral("string");
        cIdProp[QStringLiteral("description")] = QStringLiteral("Conflict ID for resolve_conflict.");
        props[QStringLiteral("conflictId")] = cIdProp;
        QJsonObject resProp;
        resProp[QStringLiteral("type")] = QStringLiteral("string");
        resProp[QStringLiteral("description")] = QStringLiteral("Resolution explanation for resolve_conflict.");
        props[QStringLiteral("resolution")] = resProp;
        QJsonObject canonProp;
        canonProp[QStringLiteral("type")] = QStringLiteral("string");
        canonProp[QStringLiteral("description")] = QStringLiteral("Canon ruling established.");
        props[QStringLiteral("canonRuling")] = canonProp;
        QJsonObject tgtChProp;
        tgtChProp[QStringLiteral("type")] = QStringLiteral("integer");
        props[QStringLiteral("targetChapter")] = tgtChProp;
        QJsonObject statProp;
        statProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("status")] = statProp;
        QJsonObject fuProp;
        fuProp[QStringLiteral("type")] = QStringLiteral("array");
        fuProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("neededFollowUps")] = fuProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("action")};
        schema[QStringLiteral("additionalProperties")] = true;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 33. update_project_metadata
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("update_project_metadata");
        t[QStringLiteral("description")] = QStringLiteral("Updates standard AO3 work metadata tags (ratings, archive warnings, categories, fandoms, relationships, characters, freeform tags) in metadata.xml.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject titleProp;
        titleProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("title")] = titleProp;
        QJsonObject authProp;
        authProp[QStringLiteral("type")] = QStringLiteral("string");
        props[QStringLiteral("author")] = authProp;
        QJsonObject ratingProp;
        ratingProp[QStringLiteral("type")] = QStringLiteral("string");
        ratingProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("General Audiences"), QStringLiteral("Teen And Up Audiences"),
                                                        QStringLiteral("Mature"), QStringLiteral("Explicit"), QStringLiteral("Not Rated")};
        props[QStringLiteral("rating")] = ratingProp;
        QJsonObject warnProp;
        warnProp[QStringLiteral("type")] = QStringLiteral("array");
        warnProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("archiveWarnings")] = warnProp;
        QJsonObject catProp;
        catProp[QStringLiteral("type")] = QStringLiteral("array");
        catProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("categories")] = catProp;
        QJsonObject fanProp;
        fanProp[QStringLiteral("type")] = QStringLiteral("array");
        fanProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("fandoms")] = fanProp;
        QJsonObject relProp;
        relProp[QStringLiteral("type")] = QStringLiteral("array");
        relProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("relationships")] = relProp;
        QJsonObject charProp;
        charProp[QStringLiteral("type")] = QStringLiteral("array");
        charProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("characters")] = charProp;
        QJsonObject tagProp;
        tagProp[QStringLiteral("type")] = QStringLiteral("array");
        tagProp[QStringLiteral("items")] = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
        props[QStringLiteral("tags")] = tagProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 34. list_image_embeds
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("list_image_embeds");
        t[QStringLiteral("description")] = QStringLiteral("Lists all external image embeds across chapters in the project (or for a specific chapter), including image URLs, alt texts, titles, dimensions, and chapter indices.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject chProp;
        chProp[QStringLiteral("type")] = QStringLiteral("integer");
        chProp[QStringLiteral("description")] = QStringLiteral("Optional 0-based chapter index to filter image embeds. If omitted, scans all chapters.");
        props[QStringLiteral("chapterIndex")] = chProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    // 35. insert_image_embed
    {
        QJsonObject t;
        t[QStringLiteral("name")] = QStringLiteral("insert_image_embed");
        t[QStringLiteral("description")] = QStringLiteral("Inserts a standard AO3-compliant external image embed URL into a chapter, automatically validating the URL and generating properly structured AO3 HTML.");
        QJsonObject schema;
        schema[QStringLiteral("type")] = QStringLiteral("object");
        QJsonObject props;
        QJsonObject chProp;
        chProp[QStringLiteral("type")] = QStringLiteral("integer");
        chProp[QStringLiteral("description")] = QStringLiteral("The 0-based index of the chapter.");
        props[QStringLiteral("chapterIndex")] = chProp;
        QJsonObject urlProp;
        urlProp[QStringLiteral("type")] = QStringLiteral("string");
        urlProp[QStringLiteral("description")] = QStringLiteral("The public URL of the image (must begin with http:// or https://).");
        props[QStringLiteral("url")] = urlProp;
        QJsonObject altProp;
        altProp[QStringLiteral("type")] = QStringLiteral("string");
        altProp[QStringLiteral("description")] = QStringLiteral("Optional descriptive alt text for accessibility and AO3 compliance.");
        props[QStringLiteral("alt")] = altProp;
        QJsonObject titProp;
        titProp[QStringLiteral("type")] = QStringLiteral("string");
        titProp[QStringLiteral("description")] = QStringLiteral("Optional hover tooltip title text.");
        props[QStringLiteral("title")] = titProp;
        QJsonObject wProp;
        wProp[QStringLiteral("type")] = QStringLiteral("integer");
        wProp[QStringLiteral("description")] = QStringLiteral("Optional display width in pixels.");
        props[QStringLiteral("width")] = wProp;
        QJsonObject hProp;
        hProp[QStringLiteral("type")] = QStringLiteral("integer");
        hProp[QStringLiteral("description")] = QStringLiteral("Optional display height in pixels.");
        props[QStringLiteral("height")] = hProp;
        QJsonObject alignProp;
        alignProp[QStringLiteral("type")] = QStringLiteral("string");
        alignProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("center"), QStringLiteral("inline"), QStringLiteral("left"), QStringLiteral("right")};
        alignProp[QStringLiteral("description")] = QStringLiteral("Alignment of the image block (default: 'center').");
        props[QStringLiteral("alignment")] = alignProp;
        QJsonObject wrapProp;
        wrapProp[QStringLiteral("type")] = QStringLiteral("boolean");
        wrapProp[QStringLiteral("description")] = QStringLiteral("Whether to wrap the image in an <a> link to the full-size image URL (default: true).");
        props[QStringLiteral("wrapInLink")] = wrapProp;
        QJsonObject posProp;
        posProp[QStringLiteral("type")] = QStringLiteral("string");
        posProp[QStringLiteral("enum")] = QJsonArray{QStringLiteral("append"), QStringLiteral("prepend")};
        posProp[QStringLiteral("description")] = QStringLiteral("Where to insert the image (default: 'append').");
        props[QStringLiteral("position")] = posProp;
        QJsonObject modelProp;
        modelProp[QStringLiteral("type")] = QStringLiteral("string");
        modelProp[QStringLiteral("description")] = QStringLiteral("Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro').");
        props[QStringLiteral("model")] = modelProp;
        schema[QStringLiteral("properties")] = props;
        schema[QStringLiteral("required")] = QJsonArray{QStringLiteral("chapterIndex"), QStringLiteral("url")};
        schema[QStringLiteral("additionalProperties")] = false;
        t[QStringLiteral("inputSchema")] = schema;
        tools.append(t);
    }

    return tools;
}
